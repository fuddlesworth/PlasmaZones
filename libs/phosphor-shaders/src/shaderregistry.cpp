// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShaders/ShaderRegistry.h>
#include <PhosphorShaders/ShaderParamPreamble.h>
#include "shaderutils.h"

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

// Namespace UUID for generating deterministic shader IDs (UUID v5)
static const QUuid ShaderNamespaceUuid = QUuid::fromString(QStringLiteral("{a1b2c3d4-e5f6-4a5b-8c9d-0e1f2a3b4c5d}"));

// Uniform name components for slot mapping
static const char* const UniformVecNames[] = {"customParams1", "customParams2", "customParams3", "customParams4",
                                              "customParams5", "customParams6", "customParams7", "customParams8"};
static const char* const UniformComponents[] = {"_x", "_y", "_z", "_w"};
static const char* const UniformColorNames[] = {"customColor1",  "customColor2",  "customColor3",  "customColor4",
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
        if (slot >= 0 && slot < 16) {
            return QString::fromLatin1(UniformColorNames[slot]);
        }
        return QString();
    }

    // Image slots 0-3 -> uTexture0-3
    if (type == QLatin1String("image")) {
        if (slot >= 0 && slot < 4) {
            return QStringLiteral("uTexture%1").arg(slot);
        }
        return QString();
    }

    // Float/int/bool slots 0-31 -> customParams1_x through customParams8_w
    if (slot >= 0 && slot < 32) {
        const int vecIndex = slot / 4;
        const int compIndex = slot % 4;
        return QString::fromLatin1(UniformVecNames[vecIndex]) + QString::fromLatin1(UniformComponents[compIndex]);
    }

    return QString();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Construction / Destruction
// ═══════════════════════════════════════════════════════════════════════════════

namespace {

/// Parse a metadata.json root into a ShaderInfo. The caller (the
/// strategy) has already validated the file exists, fits under
/// kMaxFileBytes, parses as JSON, and has an object root. We own only
/// the schema-specific bits.
/// Resolve a pack-declared file name against the pack's own directory, or
/// return empty when it escapes.
///
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
QString resolveWithinPack(const QDir& dir, const QString& declaredName,
                          PhosphorFsLoader::AbsolutePathPolicy policy = PhosphorFsLoader::AbsolutePathPolicy::Reject)
{
    // An empty declared name is ABSENT, not an escape. `toString(default)` hands
    // back an explicit `""` rather than the default (an empty string IS a
    // string), so without this the guard refused it and warned "declared a path
    // outside its own directory: ''", which points the pack author at the wrong
    // problem. Unreachable via the live scan (the schema sets minLength 1) but
    // reachable through parsePackMetadata, which has no schema gate.
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

    // Default name from directory name; the metadata `id` field overrides
    // the UUID source if present, the `name` field overrides display
    // only.
    const QString shaderName = dir.dirName();
    const QString metadataId = root.value(QLatin1String("id")).toString(shaderName);
    info.id = QUuid::createUuidV5(ShaderNamespaceUuid, metadataId).toString();
    info.name = root.value(QLatin1String("name")).toString(shaderName);
    info.description = root.value(QLatin1String("description")).toString();
    info.author = root.value(QLatin1String("author")).toString();
    info.version = root.value(QLatin1String("version")).toString(QStringLiteral("1.0"));
    info.category = root.value(QLatin1String("category")).toString();

    // Fragment shader path (default: effect.frag).
    const QString fragShaderName = root.value(QLatin1String("fragmentShader")).toString(QStringLiteral("effect.frag"));
    info.sourcePath = resolveWithinPack(dir, fragShaderName);
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
        const QString localVert = dir.filePath(QStringLiteral("zone.vert"));
        if (QFile::exists(localVert)) {
            info.vertexShaderPath = localVert;
        }
    }

    // Multi-pass: one or more buffer pass shaders (A->B->C->D).
    info.isMultipass = root.value(QLatin1String("multipass")).toBool(false);
    const QJsonArray bufferShadersArray = root.value(QLatin1String("bufferShaders")).toArray();
    if (bufferShadersArray.size() > 4) {
        qCWarning(lcShaderRegistry) << "Shader pack" << info.name << "declares" << bufferShadersArray.size()
                                    << "buffer shaders; only the first 4 are used";
    }
    bool bufferShadersDeclared = false;
    if (!bufferShadersArray.isEmpty()) {
        bufferShadersDeclared = true;
        for (int i = 0; i < qMin(bufferShadersArray.size(), 4); ++i) {
            const QString name = bufferShadersArray.at(i).toString();
            const QString resolved = resolveWithinPack(dir, name);
            if (resolved.isEmpty()) {
                // The whole list goes, and multipass with it. These entries are
                // POSITIONALLY aligned with the per-buffer wrap/filter overrides
                // read below, so silently compacting one out would shift every
                // later override onto the wrong buffer and corrupt the A→B→C
                // chain rather than refuse it. Same contract the animation
                // registry's equivalent block states.
                qCWarning(lcShaderRegistry)
                    << "Shader pack" << info.name << "declared a buffer shader outside its own directory —"
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
        if (!resolvedBuffer.isEmpty()) {
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
    info.bufferScale = qBound(0.125, scale, 1.0);
    info.bufferWrap = normalizeWrapMode(root.value(QLatin1String("bufferWrap")).toString(QStringLiteral("clamp")));
    info.useDepthBuffer = root.value(QLatin1String("depthBuffer")).toBool(false);

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

    // Parameters
    const QJsonArray paramsArray = root.value(QLatin1String("parameters")).toArray();
    for (const QJsonValue& paramValue : paramsArray) {
        QJsonObject paramObj = paramValue.toObject();
        ShaderRegistry::ParameterInfo param;
        param.id = paramObj.value(QLatin1String("id")).toString();
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
            (poolOf(p.type) == 1 ? usedColor : poolOf(p.type) == 2 ? usedImage : usedScalar).insert(p.slot);
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
    }

    // Presets
    const QJsonObject presetsObj = root.value(QLatin1String("presets")).toObject();
    for (auto it = presetsObj.begin(); it != presetsObj.end(); ++it) {
        const QJsonObject values = it.value().toObject();
        QVariantMap presetValues;
        for (auto vit = values.begin(); vit != values.end(); ++vit) {
            presetValues[vit.key()] = vit.value().toVariant();
        }
        if (!presetValues.isEmpty()) {
            info.presets[it.key()] = presetValues;
        }
    }

    return info;
}

/// Process-wide shader-metadata schema validator, compiled once from the
/// RCC-embedded schema (see qt6_add_resources in CMakeLists).
const PhosphorFsLoader::SchemaValidator& shaderMetadataValidator()
{
    static const PhosphorFsLoader::SchemaValidator validator = PhosphorFsLoader::SchemaValidator::fromResource(
        QStringLiteral(":/phosphorshaders/schemas/shader-metadata.schema.json"), lcShaderRegistry());
    return validator;
}

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

    // Construct the URL pointing at the fragment shader.
    info.shaderUrl = QUrl::fromLocalFile(info.sourcePath);

    // Optional preview image.
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
    // `parseShader` rejects entries whose `sourcePath` doesn't exist, so
    // every payload reaching this callback has a valid frag path — no
    // empty-check needed.
    QDir dir(QFileInfo(info.sourcePath).absolutePath());
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
        hasher.addData(QByteArray::number(fi.size()));
        hasher.addData(QByteArray::number(fi.lastModified().toMSecsSinceEpoch()));
    };
    // metadata.json sits in the same directory as the frag source.
    if (!info.sourcePath.isEmpty()) {
        mixFile(QFileInfo(info.sourcePath).absolutePath() + QStringLiteral("/metadata.json"));
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
    // Registry iteration is insertion order; sort by id for alphabetical
    // output (the legacy strategy returned sorted).
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
    map[QStringLiteral("bufferShaderPaths")] = info.bufferShaderPaths;
    map[QStringLiteral("bufferFeedback")] = info.bufferFeedback;
    map[QStringLiteral("bufferScale")] = info.bufferScale;
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
// Parameters, Presets & Validation
// ═══════════════════════════════════════════════════════════════════════════════

QVariantMap ShaderRegistry::presetParams(const QString& shaderId, const QString& presetName) const
{
    const ShaderInfo info = shader(shaderId);
    if (!info.isValid() || !info.presets.contains(presetName)) {
        return {};
    }
    // Validate and fill defaults for any missing parameters
    return validateAndCoerceParams(shaderId, info.presets.value(presetName));
}

QStringList ShaderRegistry::shaderPresetNames(const QString& shaderId) const
{
    const ShaderInfo info = shader(shaderId);
    if (!info.isValid()) {
        return {};
    }
    return info.presets.keys();
}

QVariantList ShaderRegistry::shaderPresetsVariant(const QString& shaderId) const
{
    const ShaderInfo info = shader(shaderId);
    if (!info.isValid()) {
        return {};
    }
    QVariantList result;
    for (auto it = info.presets.constBegin(); it != info.presets.constEnd(); ++it) {
        QVariantMap entry;
        entry[QStringLiteral("name")] = it.key();
        entry[QStringLiteral("params")] = it.value();
        result.append(entry);
    }
    return result;
}

bool ShaderRegistry::validateParams(const QString& id, const QVariantMap& params) const
{
    const ShaderInfo info = shader(id);
    if (!info.isValid()) {
        return false;
    }

    for (const ParameterInfo& param : info.parameters) {
        if (params.contains(param.id)) {
            if (!validateParameterValue(param, params.value(param.id))) {
                qCWarning(lcShaderRegistry) << "Invalid shader parameter:" << param.id << "for shader:" << id;
                return false;
            }
        }
    }
    return true;
}

bool ShaderRegistry::validateParameterValue(const ParameterInfo& param, const QVariant& value) const
{
    if (param.type == QLatin1String("float")) {
        bool ok = false;
        double v = value.toDouble(&ok);
        if (!ok)
            return false;
        if (param.minValue.isValid() && v < param.minValue.toDouble())
            return false;
        if (param.maxValue.isValid() && v > param.maxValue.toDouble())
            return false;
    } else if (param.type == QLatin1String("int")) {
        bool ok = false;
        int v = value.toInt(&ok);
        if (!ok)
            return false;
        if (param.minValue.isValid() && v < param.minValue.toInt())
            return false;
        if (param.maxValue.isValid() && v > param.maxValue.toInt())
            return false;
    } else if (param.type == QLatin1String("color")) {
        QColor c(value.toString());
        if (!c.isValid())
            return false;
    } else if (param.type == QLatin1String("bool")) {
        if (!value.canConvert<bool>())
            return false;
    } else if (param.type == QLatin1String("image")) {
        if (!value.canConvert<QString>())
            return false;
    } else {
        // Unknown type: fail CLOSED.
        //
        // Currently UNREACHABLE on every live path, and deliberately kept as a
        // backstop rather than because a caller needs it today: `type` is a
        // required enum in data/schemas/shader-metadata.schema.json and the schema
        // gate runs before the metadata is parsed, so every value arriving here
        // carries a validated type. (`validateParams` has no callers at all, and
        // `validateAndCoerceParams` is reached only from `presetParams`, which
        // feeds it presets from that same validated metadata.) The point is that a
        // FUTURE caller bypassing the schema gate should hit something here rather
        // than have an unrecognised type waved through.
        return false;
    }
    return true;
}

QVariantMap ShaderRegistry::validateAndCoerceParams(const QString& id, const QVariantMap& params) const
{
    QVariantMap result;
    const ShaderInfo info = shader(id);
    if (!info.isValid()) {
        return result;
    }

    for (const ParameterInfo& param : info.parameters) {
        if (params.contains(param.id) && validateParameterValue(param, params.value(param.id))) {
            result[param.id] = params.value(param.id);
        } else {
            result[param.id] = param.defaultValue;
        }
    }
    return result;
}

QVariantMap ShaderRegistry::defaultParams(const QString& id) const
{
    QVariantMap result;
    const ShaderInfo info = shader(id);
    for (const ParameterInfo& param : info.parameters) {
        result[param.id] = param.defaultValue;
    }
    return result;
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
                const QDir shaderDir = QFileInfo(info.sourcePath).absoluteDir();
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
            // on an unbound sampler is meaningless, and the same consumer reads
            // all three maps.
            if (!result.value(uName).toString().isEmpty()) {
                result[uName + QStringLiteral("_wrap")] = param.wrap.isEmpty() ? QStringLiteral("clamp") : param.wrap;
            }

            // Pass through SVG render size if present in stored params
            const QString svgSizeKey = param.id + QStringLiteral("_svgSize");
            if (storedParams.contains(svgSizeKey)) {
                result[uName + QStringLiteral("_svgSize")] = storedParams.value(svgSizeKey);
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

ShaderRegistry::ShaderInfo ShaderRegistry::parsePackMetadata(const QString& packDir, QString* error)
{
    const QString metaPath = QDir(packDir).filePath(QStringLiteral("metadata.json"));
    QFile file(metaPath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("cannot open %1").arg(metaPath);
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
    // parseShaderMetadata lives in this TU's anonymous namespace; it sets
    // sourcePath / vertexShaderPath / bufferShaderPaths from packDir and applies
    // the same auto-slot assignment the live scan does.
    return parseShaderMetadata(packDir, doc.object());
}

} // namespace PhosphorShaders
