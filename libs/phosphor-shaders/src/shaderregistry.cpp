// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShaders/ShaderRegistry.h>
#include <PhosphorShaders/CustomParamsKey.h>
#include <PhosphorShaders/ShaderParamPreamble.h>
#include "shaderregistry_parse.h"
#include "shaderutils.h"

#include <PhosphorFsLoader/DirectoryLoader.h>
#include <PhosphorFsLoader/PackPathGuard.h>
#include <PhosphorFsLoader/SchemaValidator.h>

#include <QColor>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSet>
#include <QUrl>
#include <QUuid>

#include <algorithm>

namespace PhosphorShaders {

Q_LOGGING_CATEGORY(lcShaderRegistry, "phosphorshaders.shaderregistry")

// The deterministic shader-id namespace UUID lives with the parser in
// shaderregistry_parse.cpp (its only consumer).

// Uniform name components for slot mapping
static const char* const UNIFORM_VEC_NAMES[] = {"customParams1", "customParams2", "customParams3", "customParams4",
                                                "customParams5", "customParams6", "customParams7", "customParams8"};
static const char* const UNIFORM_COMPONENTS[] = {"_x", "_y", "_z", "_w"};
static const char* const UNIFORM_COLOR_NAMES[] = {"customColor1",  "customColor2",  "customColor3",  "customColor4",
                                                  "customColor5",  "customColor6",  "customColor7",  "customColor8",
                                                  "customColor9",  "customColor10", "customColor11", "customColor12",
                                                  "customColor13", "customColor14", "customColor15", "customColor16"};

QString ShaderRegistry::ParameterInfo::uniformName() const
{
    if (slot < 0) {
        return QString();
    }

    if (type == QLatin1String("color")) {
        // Color slots 0-15 -> customColor1-16
        if (slot >= 0 && slot < CustomColors::kColorCount) {
            return QString::fromLatin1(UNIFORM_COLOR_NAMES[slot]);
        }
        return QString();
    }

    // Image slots 0-3 -> uTexture0-3
    if (type == QLatin1String("image")) {
        if (slot >= 0 && slot < kMaxImageSlots) {
            return QStringLiteral("uTexture%1").arg(slot);
        }
        return QString();
    }

    // Float/int/bool slots 0-31 -> customParams1_x through customParams8_w
    if (slot >= 0 && slot < CustomParams::kFlatSlotCount) {
        const int vecIndex = slot / 4;
        const int compIndex = slot % 4;
        return QString::fromLatin1(UNIFORM_VEC_NAMES[vecIndex]) + QString::fromLatin1(UNIFORM_COMPONENTS[compIndex]);
    }

    return QString();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Construction / Destruction
// ═══════════════════════════════════════════════════════════════════════════════

// The metadata parse machinery (resolveWithinPack, parseShaderMetadata,
// shaderMetadataValidator, and ShaderRegistry::parsePackMetadata) moved to
// shaderregistry_parse.cpp — split by concern; this TU keeps the loader
// wiring, the lookup surface, and the uniform translation.
using ShaderRegistryParse::parseShaderMetadata;
using ShaderRegistryParse::resolveWithinPack;
using ShaderRegistryParse::shaderMetadataValidator;

namespace {

/// Strategy parser callback: parse + validate. Returns std::nullopt to
/// skip the shader (missing frag, broken multipass, etc.); the strategy
/// logs the per-file context.
std::optional<ShaderRegistry::ShaderInfo> parseShader(const QString& shaderDir, const QJsonObject& root, bool isUserDir)
{
    // Structural schema gate on metadata.json (identity + parameter contract)
    // before parsing, so a malformed pack is skipped with a clear diagnostic
    // rather than registering a shader with unbindable parameters.
    if (const auto errors = shaderMetadataValidator().validate(root)) {
        qCWarning(lcShaderRegistry) << "Skipping shader pack failing schema validation:" << shaderDir;
        PhosphorFsLoader::logSchemaErrors(lcShaderRegistry(), *errors);
        return std::nullopt;
    }

    ShaderRegistry::ShaderInfo info = parseShaderMetadata(shaderDir, root);
    info.isUserShader = isUserDir;

    // Multipass requires at least one buffer shader; missing → skip.
    if (info.isMultipass && info.bufferShaderPaths.isEmpty()) {
        qCWarning(lcShaderRegistry) << "Skipping multipass shader (missing buffer shader(s)):" << shaderDir;
        return std::nullopt;
    }

    // Fragment shader must exist on disk.
    if (!QFile::exists(info.sourcePath)) {
        qCWarning(lcShaderRegistry) << "Shader missing fragment shader:" << info.sourcePath;
        return std::nullopt;
    }

    // shaderUrl is now set inside parseShaderMetadata (shared with the offline
    // parsePackMetadata path), so no assignment is needed here.

    // Optional preview image. Deliberately excluded from the watch set and
    // the content signature: the preview never feeds the GPU pipeline, so an
    // edited preview.png re-registering (and re-baking) the pack would be
    // pure churn; a stale thumbnail until the next rescan is acceptable.
    QDir dir(shaderDir);
    const QString previewPath = dir.filePath(QStringLiteral("preview.png"));
    if (QFile::exists(previewPath)) {
        info.previewPath = previewPath;
    }

    return info;
}

/// Per-payload watch list — the shader subdir's `*.frag/*.vert/*.glsl/*.json`
/// files. The strategy already adds the metadata.json itself, so we skip
/// it here to avoid a duplicate watch entry; everything else (auxiliary
/// presets / settings JSON, additional GLSL includes, the frag/vert
/// pair, the buffer-pass shaders for multipass) is added so atomic-
/// rename saves on any of them re-fire the rescan.
QStringList shaderEntryWatchPaths(const ShaderRegistry::ShaderInfo& info)
{
    // The pack root, not the frag's directory — a custom `fragmentShader`
    // may sit in a pack subdirectory, and the watch glob must cover the
    // directory metadata.json lives in. Fallback for hand-built ShaderInfo
    // (tests) that set only sourcePath.
    QDir dir(info.packDir.isEmpty() ? QFileInfo(info.sourcePath).absolutePath() : info.packDir);
    const QStringList shaderFiles = dir.entryList(
        {QStringLiteral("*.frag"), QStringLiteral("*.vert"), QStringLiteral("*.glsl"), QStringLiteral("*.json")},
        QDir::Files);
    QStringList paths;
    paths.reserve(shaderFiles.size());
    for (const QString& f : shaderFiles) {
        if (f == QLatin1String("metadata.json")) {
            // Already covered by the strategy's per-entry add — skipping
            // here keeps the watch set minimal and the per-file
            // diagnostics clean (the base dedups silently, but emitting
            // both wastes the dedup pass).
            continue;
        }
        paths.append(dir.filePath(f));
    }
    // The pack-root glob above is non-recursive, so a pack whose frag/vert/
    // buffer sits in a SUBDIRECTORY (the very shape the packDir field exists to
    // support) would not be watched and would silently miss live-reload.
    // Append the resolved compiled paths explicitly — they are absolute and the
    // loader dedups against the glob entries — mirroring the surface registry's
    // effectWatchPaths.
    const auto appendIfNew = [&paths](const QString& p) {
        if (!p.isEmpty() && !paths.contains(p)) {
            paths.append(p);
        }
    };
    appendIfNew(info.sourcePath);
    appendIfNew(info.vertexShaderPath);
    for (const QString& buf : info.bufferShaderPaths) {
        appendIfNew(buf);
    }
    return paths;
}

QStringList shaderTopLevelWatchPaths(const QString& searchPath)
{
    QStringList paths;
    QDir dir(searchPath);
    if (!dir.exists()) {
        return paths;
    }
    const QStringList nameFilters = {QStringLiteral("*.frag"), QStringLiteral("*.vert"), QStringLiteral("*.glsl"),
                                     QStringLiteral("*.json")};
    const QStringList topFiles = dir.entryList(nameFilters, QDir::Files);
    for (const QString& f : topFiles) {
        paths.append(dir.filePath(f));
    }
    QDir sharedDir(searchPath + QStringLiteral("/shared"));
    if (sharedDir.exists()) {
        const QStringList sharedFiles = sharedDir.entryList(nameFilters, QDir::Files);
        for (const QString& f : sharedFiles) {
            paths.append(sharedDir.filePath(f));
        }
    }
    return paths;
}

bool shaderSubdirSkip(const QString& subdirName)
{
    return subdirName == QLatin1String("none") || subdirName == QLatin1String("shared");
}

// Per-entry content signature: the path|size|mtime of every file that
// defines a pack — its metadata.json plus frag/vert/buffer shaders. Drives
// MetadataPackLoader's per-entry reconcile so an edit to a pack's metadata
// OR its shader sources re-registers THAT pack (fresh ShaderInfo) while
// leaving unedited siblings untouched. Edits to SHARED includes
// (common.glsl, audio.glsl, the default zone.vert) belong to no single
// pack — they reach the loader's coarse onCommitted hook via the
// per-directory watch set (shaderTopLevelWatchPaths) and re-emit
// shadersChanged without per-pack churn.
void shaderContentSignature(QCryptographicHash& hasher, const ShaderRegistry::ShaderInfo& info)
{
    const auto mixFile = [&hasher](const QString& path) {
        if (path.isEmpty()) {
            return;
        }
        const QFileInfo fi(path);
        hasher.addData(path.toUtf8());
        if (fi.exists()) {
            hasher.addData(QByteArray::number(fi.size()));
            hasher.addData(QByteArray::number(fi.lastModified().toMSecsSinceEpoch()));
        } else {
            // Stable sentinel for absent files: lastModified() on an invalid
            // datetime is implementation-defined (same contract as
            // MetadataPackScanStrategy's watch-set hash).
            hasher.addData(QByteArrayView("missing"));
        }
    };
    // metadata.json lives at the pack root, which is NOT necessarily the
    // frag's directory (a custom `fragmentShader` may sit in a subdirectory).
    const QString metaBase = !info.packDir.isEmpty() ? info.packDir
        : !info.sourcePath.isEmpty()                 ? QFileInfo(info.sourcePath).absolutePath()
                                                     : QString();
    if (!metaBase.isEmpty()) {
        mixFile(metaBase + QStringLiteral("/metadata.json"));
    }
    mixFile(info.sourcePath);
    mixFile(info.vertexShaderPath);
    for (const QString& buf : info.bufferShaderPaths) {
        mixFile(buf);
    }
    // isUser is set from the user-path classification, NOT from file content —
    // it can flip (setUserPath after addSearchPaths) with no file change, so
    // mix it in or the reconcile would keep the stale-classification entry.
    hasher.addData(info.isUserShader ? "u" : "s");
}

} // namespace

ShaderRegistry::ShaderRegistry(QObject* parent)
    : QObject(parent)
    , m_loader(std::make_unique<PhosphorRegistry::MetadataPackLoader<ShaderPack>>(
          &m_registry,
          // Parser: reuse the existing metadata→ShaderInfo parse, then wrap
          // the result in a ShaderPack for the registry.
          [](const QString& subdir, const QJsonObject& root, bool isUser) -> std::shared_ptr<ShaderPack> {
              std::optional<ShaderInfo> info = parseShader(subdir, root, isUser);
              return info ? std::make_shared<ShaderPack>(std::move(*info)) : nullptr;
          },
          lcShaderRegistry()))
{
    // Watch each pack's frag/vert/buffer sources (per-entry) + the shared
    // top-level includes (per-directory); skip the "none"/"shared" sentinel
    // subdirs. The per-entry content signature drives the reconcile so a
    // metadata- or source-edited pack re-registers with fresh info; the
    // coarse onCommitted hook re-emits shadersChanged on any committed
    // rescan (incl. shared-include edits), matching the legacy registry's
    // single shadersChanged-on-any-change contract.
    m_loader->setPerEntryWatchPaths([](const ShaderPack& p) {
        return shaderEntryWatchPaths(p.info());
    });
    m_loader->setPerDirectoryWatchPaths(shaderTopLevelWatchPaths);
    m_loader->setPerSubdirSkip(shaderSubdirSkip);
    m_loader->setSignatureContrib([](QCryptographicHash& hasher, const ShaderPack& p) {
        shaderContentSignature(hasher, p.info());
    });
    // Q_EMIT through `this`: a most-derived ctor (PlasmaZones::ShaderRegistry)
    // may call addSearchPaths from its own body, firing this while still on
    // the stack — harmless, nothing has connected yet.
    m_loader->setOnCommitted([this]() {
        Q_EMIT shadersChanged();
    });
}

ShaderRegistry::~ShaderRegistry() = default;

// ── Search paths (forwarded to the loader) ───────────────────────────────

void ShaderRegistry::addSearchPath(const QString& path, PhosphorFsLoader::LiveReload liveReload)
{
    m_loader->addSearchPath(path, liveReload);
}

void ShaderRegistry::addSearchPaths(const QStringList& paths, PhosphorFsLoader::LiveReload liveReload,
                                    PhosphorFsLoader::RegistrationOrder order)
{
    m_loader->addSearchPaths(paths, liveReload, order);
}

QStringList ShaderRegistry::searchPaths() const
{
    return m_loader->searchPaths();
}

void ShaderRegistry::setUserPath(const QString& path)
{
    m_loader->setUserPath(path);
}

void ShaderRegistry::refresh()
{
    m_loader->refresh();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Shader Identity Helpers
// ═══════════════════════════════════════════════════════════════════════════════

QString ShaderRegistry::noneShaderUuid()
{
    // Empty string means "no shader" — keeps things simple
    return QString();
}

bool ShaderRegistry::isNoneShader(const QString& id)
{
    return id.isEmpty();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Shader Discovery & Loading
// ═══════════════════════════════════════════════════════════════════════════════

void ShaderRegistry::reportShaderBakeStarted(const QString& shaderId)
{
    Q_EMIT shaderCompilationStarted(shaderId);
}

void ShaderRegistry::reportShaderBakeFinished(const QString& shaderId, bool success, const QString& error)
{
    Q_EMIT shaderCompilationFinished(shaderId, success, error);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Query Methods
// ═══════════════════════════════════════════════════════════════════════════════

QList<ShaderRegistry::ShaderInfo> ShaderRegistry::availableShaders() const
{
    // Registry iteration is insertion order; sort by id for deterministic
    // output across rescans (the ids are UUIDv5 strings, so the order is
    // stable but not humanly alphabetical; the legacy strategy returned
    // sorted too).
    QList<ShaderInfo> result;
    result.reserve(m_registry.size());
    m_registry.forEach([&result](const std::shared_ptr<ShaderPack>& pack) {
        result.append(pack->info());
    });
    std::sort(result.begin(), result.end(), [](const ShaderInfo& a, const ShaderInfo& b) {
        return a.id < b.id;
    });
    return result;
}

QVariantList ShaderRegistry::availableShadersVariant() const
{
    const QList<ShaderInfo> sorted = availableShaders();
    QVariantList result;
    result.reserve(sorted.size());
    for (const ShaderInfo& info : sorted) {
        result.append(shaderInfoToVariantMap(info));
    }
    return result;
}

ShaderRegistry::ShaderInfo ShaderRegistry::shader(const QString& id) const
{
    const auto pack = m_registry.factory(id);
    return pack ? pack->info() : ShaderInfo{};
}

QVariantMap ShaderRegistry::shaderInfo(const QString& id) const
{
    const auto pack = m_registry.factory(id);
    if (!pack) {
        return QVariantMap();
    }
    return shaderInfoToVariantMap(pack->info());
}

QUrl ShaderRegistry::shaderUrl(const QString& id) const
{
    if (isNoneShader(id)) {
        return QUrl();
    }
    const auto pack = m_registry.factory(id);
    return pack ? pack->info().shaderUrl : QUrl();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Variant Map Conversion (merged from params.cpp)
// ═══════════════════════════════════════════════════════════════════════════════

QVariantMap ShaderRegistry::shaderInfoToVariantMap(const ShaderInfo& info) const
{
    QVariantMap map;
    // Required fields (always set to non-empty strings)
    map[QStringLiteral("id")] = info.id.isEmpty() ? QStringLiteral("unknown") : info.id;
    map[QStringLiteral("name")] = info.name.isEmpty() ? info.id : info.name;
    map[QStringLiteral("description")] = info.description; // Empty string is OK
    map[QStringLiteral("author")] = info.author;
    map[QStringLiteral("version")] = info.version;
    map[QStringLiteral("isUserShader")] = info.isUserShader;
    map[QStringLiteral("category")] = info.category;
    map[QStringLiteral("isValid")] = info.isValid();

    // Optional fields - only include if non-empty
    if (info.shaderUrl.isValid()) {
        map[QStringLiteral("shaderUrl")] = info.shaderUrl.toString();
    } else {
        map[QStringLiteral("shaderUrl")] = QString(); // Empty string, not null
    }

    if (!info.previewPath.isEmpty()) {
        map[QStringLiteral("previewPath")] = info.previewPath;
    } else {
        map[QStringLiteral("previewPath")] = QString(); // Empty string, not null
    }

    // Multipass shader metadata
    map[QStringLiteral("multipass")] = info.isMultipass;
    map[QStringLiteral("bufferShaderPaths")] = info.bufferShaderPaths;
    map[QStringLiteral("bufferFeedback")] = info.bufferFeedback;
    map[QStringLiteral("bufferScale")] = info.bufferScale;
    map[QStringLiteral("halfFloatBuffers")] = info.halfFloatBuffers;
    map[QStringLiteral("bufferWrap")] = info.bufferWrap;
    if (!info.bufferWraps.isEmpty()) {
        map[QStringLiteral("bufferWraps")] = QVariant::fromValue(info.bufferWraps);
    }
    map[QStringLiteral("bufferFilter")] = info.bufferFilter;
    if (!info.bufferFilters.isEmpty()) {
        map[QStringLiteral("bufferFilters")] = QVariant::fromValue(info.bufferFilters);
    }
    // Keys match metadata.json names ("wallpaper", "depthBuffer"), not Q_PROPERTY names
    map[QStringLiteral("wallpaper")] = info.useWallpaper;
    map[QStringLiteral("depthBuffer")] = info.useDepthBuffer;

    // Parameters list (empty list is OK)
    QVariantList params;
    for (const ParameterInfo& param : info.parameters) {
        params.append(parameterInfoToVariantMap(param));
    }
    map[QStringLiteral("parameters")] = params;

    // Presets list
    QVariantList presetsList;
    for (auto it = info.presets.constBegin(); it != info.presets.constEnd(); ++it) {
        QVariantMap presetMap;
        presetMap[QStringLiteral("name")] = it.key();
        presetMap[QStringLiteral("params")] = it.value();
        presetsList.append(presetMap);
    }
    map[QStringLiteral("presets")] = presetsList;

    return map;
}

QVariantMap ShaderRegistry::parameterInfoToVariantMap(const ParameterInfo& param) const
{
    QVariantMap map;
    map[QStringLiteral("id")] = param.id;
    map[QStringLiteral("name")] = param.name;
    map[QStringLiteral("type")] = param.type;
    map[QStringLiteral("slot")] = param.slot;
    map[QStringLiteral("mapsTo")] = param.uniformName(); // Computed from slot for compatibility
    map[QStringLiteral("useZoneColor")] = param.useZoneColor;
    if (!param.wrap.isEmpty()) {
        map[QStringLiteral("wrap")] = param.wrap;
    }

    // Only include optional values if they are valid
    if (!param.group.isEmpty()) {
        map[QStringLiteral("group")] = param.group;
    }
    if (param.defaultValue.isValid()) {
        map[QStringLiteral("default")] = param.defaultValue;
    }
    if (param.minValue.isValid()) {
        map[QStringLiteral("min")] = param.minValue;
    }
    if (param.maxValue.isValid()) {
        map[QStringLiteral("max")] = param.maxValue;
    }

    return map;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Uniform Translation
// ═══════════════════════════════════════════════════════════════════════════════

QVariantMap ShaderRegistry::translateParamsToUniforms(const QString& shaderId, const QVariantMap& storedParams) const
{
    QVariantMap result;
    const ShaderInfo info = shader(shaderId);

    if (!info.isValid() || isNoneShader(shaderId)) {
        return result;
    }

    // Build translation map from parameter definitions
    // Also start with default values for uniforms that aren't in storedParams
    for (const ParameterInfo& param : info.parameters) {
        const QString uName = param.uniformName();
        if (uName.isEmpty()) {
            continue; // Parameter doesn't map to a uniform
        }

        // Provenance, computed ONCE and used by both the value branch below and the
        // containment policy further down. Evaluating it twice let the two disagree
        // about where a value came from, which is exactly what the policy turns on.
        const bool fromUser = storedParams.contains(param.id);

        if (fromUser) {
            QVariant value = storedParams.value(param.id);

            // Handle color type - keep as QString for marshalling compatibility
            if (param.type == QLatin1String("color") && value.typeId() == QMetaType::QString) {
                QColor color(value.toString());
                if (color.isValid()) {
                    result[uName] = color.name(QColor::HexArgb);
                } else {
                    // Fallback to default or transparent black
                    QColor defColor(param.defaultValue.toString());
                    result[uName] = defColor.isValid() ? defColor.name(QColor::HexArgb) : QStringLiteral("#00000000");
                }
            } else {
                result[uName] = value;
            }
        } else {
            // Use default value for missing parameters
            if (param.type == QLatin1String("color")) {
                QColor color(param.defaultValue.toString());
                result[uName] = color.isValid() ? color.name(QColor::HexArgb) : QStringLiteral("#00000000");
            } else if (!param.defaultValue.isValid() || param.defaultValue.isNull()) {
                // Provide type-appropriate empty default to avoid null QVariant
                if (param.type == QLatin1String("image")) {
                    result[uName] = QString();
                } else if (param.type == QLatin1String("bool")) {
                    result[uName] = false;
                } else {
                    result[uName] = 0.0;
                }
            } else {
                result[uName] = param.defaultValue;
            }
        }

        // For image parameters: resolve against the shader directory, emit wrap mode.
        //
        // The POLICY depends on where the value came from, which is why the two
        // sources are distinguished above rather than read off the merged result.
        // A value the USER picked (`storedParams`, from the file picker or D-Bus)
        // may legitimately be an absolute path outside any pack. A value the PACK
        // declared may not: an absolute default is either a mistake or an attempt
        // to have the consumer bind an arbitrary file as a texture, so it is
        // containment-checked like every other pack-declared path. Both sibling
        // registries already Reject at parse time; this one had no image-path
        // check at all, and skipping the absolute case let a third-party pack
        // ship `"default": "/home/user/.ssh/id_rsa"` straight through.
        if (param.type == QLatin1String("image")) {
            const QString imgPath = result.value(uName).toString();
            // No `isRelative()` pre-filter: the guard itself is what decides what
            // an absolute path means, per policy. Short-circuiting on relative-ness
            // used to skip the guard entirely for a trusted absolute path, which
            // also skipped the cleanPath normalisation every other accepted return
            // gets — so one user-picked file could reach watch keys and path-keyed
            // caches in two spellings.
            if (!imgPath.isEmpty()) {
                // Containment is against the PACK root: a subdir-frag pack's
                // image defaults are declared relative to metadata.json, not
                // the frag's directory.
                const QDir shaderDir =
                    info.packDir.isEmpty() ? QFileInfo(info.sourcePath).absoluteDir() : QDir(info.packDir);
                const QString resolved = resolveWithinPack(shaderDir, imgPath,
                                                           fromUser ? PhosphorFsLoader::AbsolutePathPolicy::Trust
                                                                    : PhosphorFsLoader::AbsolutePathPolicy::Reject);
                // Assigned unconditionally once containment has been decided.
                // Leaving the original RELATIVE string in place — which the old
                // `else if (exists)` shape did whenever the file was merely
                // absent — hands an unresolved, unvetted path to the consumer,
                // which then resolves it against its own base (the process CWD).
                // A refused traversal would escape anyway, one layer down.
                //
                // Empty on refusal is the fail-closed answer; the resolved
                // absolute path on acceptance, existing or not, because the
                // consumer already treats a failed load as "no texture" and
                // pre-checking existence here would only add a TOCTOU.
                result[uName] = resolved;
            }
            // Ensure image params are never null QVariant
            if (!result.value(uName).isValid() || result.value(uName).isNull()) {
                result[uName] = QString();
            }
            // Both-or-neither, matching the two sibling registries: a wrap mode
            // (and an SVG render size) on an unbound sampler is meaningless,
            // and the same consumer reads all three maps.
            if (!result.value(uName).toString().isEmpty()) {
                QString wrap = param.wrap;
                if (!wrap.isEmpty() && !isValidWrapToken(wrap)) {
                    // Same JSON-boundary vocabulary check the surface parser
                    // applies: warn and fall back rather than forwarding an
                    // unknown token for the runtime to coerce silently.
                    qCWarning(lcShaderRegistry) << "Shader" << info.id << "param" << param.id
                                                << "declares unknown wrap mode" << wrap << "- using clamp";
                    wrap.clear();
                }
                result[uName + QStringLiteral("_wrap")] = wrap.isEmpty() ? QStringLiteral("clamp") : wrap;

                // Pass through SVG render size if present in stored params
                const QString svgSizeKey = param.id + QStringLiteral("_svgSize");
                if (storedParams.contains(svgSizeKey)) {
                    result[uName + QStringLiteral("_svgSize")] = storedParams.value(svgSizeKey);
                }
            }
        }
    }

    return result;
}

QString ShaderRegistry::paramPreamble(const ShaderInfo& info)
{
    // By the time this runs, parseShaderMetadata has resolved every VALID-id
    // parameter's slot to >= 0 — an explicit metadata `slot`, or one auto-assigned
    // by declaration order when omitted (most migrated packs drop `slot`); an
    // invalid-id param keeps slot -1 and is skipped identically by buildParamPreamble
    // (no define) and translateParamsToUniforms (no upload). So each emitted
    // PreambleParam carries a concrete explicit slot (buildParamPreamble's
    // auto-numbering isn't exercised on this zone path). buildParamPreamble turns
    // each into `#define p_<id> <glsl-accessor>` using the same slot→accessor rule
    // ParameterInfo::uniformName()/translateParamsToUniforms upload to: color →
    // customColors[slot], image → uTexture<slot>, else → customParams[slot/4].
    // <xyzw>. So p_<id> reads exactly the lane the value lands in.
    QList<PreambleParam> params;
    params.reserve(info.parameters.size());
    for (const ParameterInfo& p : info.parameters) {
        PreambleParam entry;
        entry.id = p.id;
        if (p.type == QLatin1String("color")) {
            entry.pool = PreambleParam::Pool::Color;
        } else if (p.type == QLatin1String("image")) {
            entry.pool = PreambleParam::Pool::Image;
        } else {
            entry.pool = PreambleParam::Pool::Scalar;
        }
        entry.explicitSlot = p.slot;
        params.append(entry);
    }
    return buildParamPreamble(params);
}

// ShaderRegistry::parsePackMetadata is implemented in shaderregistry_parse.cpp
// alongside the shared parser it delegates to.

} // namespace PhosphorShaders
