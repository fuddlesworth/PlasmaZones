// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShaders/ShaderRegistry.h>
#include <PhosphorShaders/IWallpaperProvider.h>
#include "shaderutils.h"

#include <PhosphorFsLoader/MetadataPackScanStrategy.h>
#include <PhosphorFsLoader/WatchedDirectorySet.h>

#include <QColor>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QUrl>
#include <QUuid>

#include <QMutexLocker>

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

// Convert shader name to deterministic UUID (v5)
static QString shaderNameToUuid(const QString& name)
{
    if (name.isEmpty()) {
        return QString();
    }
    return QUuid::createUuidV5(ShaderNamespaceUuid, name).toString();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Construction / Destruction
// ═══════════════════════════════════════════════════════════════════════════════

namespace {

/// Parse a metadata.json root into a ShaderInfo. The caller (the
/// strategy) has already validated the file exists, fits under
/// kMaxFileBytes, parses as JSON, and has an object root. We own only
/// the schema-specific bits.
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

    // Fragment / vertex shader paths (default: effect.frag, zone.vert)
    const QString fragShaderName = root.value(QLatin1String("fragmentShader")).toString(QStringLiteral("effect.frag"));
    const QString vertShaderName = root.value(QLatin1String("vertexShader")).toString(QStringLiteral("zone.vert"));
    info.sourcePath = dir.filePath(fragShaderName);
    info.vertexShaderPath = dir.filePath(vertShaderName);

    // Multi-pass: one or more buffer pass shaders (A->B->C->D).
    info.isMultipass = root.value(QLatin1String("multipass")).toBool(false);
    const QJsonArray bufferShadersArray = root.value(QLatin1String("bufferShaders")).toArray();
    if (!bufferShadersArray.isEmpty()) {
        for (int i = 0; i < qMin(bufferShadersArray.size(), 4); ++i) {
            const QString name = bufferShadersArray.at(i).toString();
            if (!name.isEmpty()) {
                info.bufferShaderPaths.append(dir.filePath(name));
            }
        }
    }
    if (info.bufferShaderPaths.isEmpty()) {
        const QString bufferShaderName =
            root.value(QLatin1String("bufferShader")).toString(QStringLiteral("buffer.frag"));
        info.bufferShaderPaths.append(dir.filePath(bufferShaderName));
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

/// Strategy parser callback: parse + validate. Returns std::nullopt to
/// skip the shader (missing frag, broken multipass, etc.); the strategy
/// logs the per-file context.
std::optional<ShaderRegistry::ShaderInfo> parseShader(const QString& shaderDir, const QJsonObject& root, bool isUserDir)
{
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
/// files. The strategy already adds the metadata.json itself; this adds
/// every candidate edit target inside the shader pack so atomic-rename
/// saves on any of them re-fire the rescan.
QStringList shaderEntryWatchPaths(const ShaderRegistry::ShaderInfo& info)
{
    QStringList paths;
    if (info.sourcePath.isEmpty()) {
        return paths;
    }
    QDir dir(QFileInfo(info.sourcePath).absolutePath());
    const QStringList shaderFiles = dir.entryList(
        {QStringLiteral("*.frag"), QStringLiteral("*.vert"), QStringLiteral("*.glsl"), QStringLiteral("*.json")},
        QDir::Files);
    paths.reserve(shaderFiles.size());
    for (const QString& f : shaderFiles) {
        paths.append(dir.filePath(f));
    }
    return paths;
}

/// Per-search-path watch additions: top-level shared GLSL/JSON includes
/// (e.g. `common.glsl`, `audio.glsl`, `wallpaper.glsl`). These live
/// alongside shader subdirs and any of them changing should re-fire the
/// rescan.
QStringList shaderTopLevelWatchPaths(const QString& searchPath)
{
    QStringList paths;
    QDir dir(searchPath);
    if (!dir.exists()) {
        return paths;
    }
    const QStringList topFiles = dir.entryList({QStringLiteral("*.glsl"), QStringLiteral("*.json")}, QDir::Files);
    paths.reserve(topFiles.size());
    for (const QString& f : topFiles) {
        paths.append(dir.filePath(f));
    }
    return paths;
}

/// Skip the reserved sentinel subdirectory `none` (means "no shader" in
/// the consumer's UI).
bool shaderSubdirSkip(const QString& subdirName)
{
    return subdirName == QLatin1String("none");
}

/// Hash schema-specific bits change-detection cares about. The strategy
/// already mixes in `id`; this contributor adds path tuples, isUser,
/// and frag mtime+size so an in-place edit on the fragment shader
/// surfaces as a content change even when no metadata bytes moved.
void contributeShaderSignature(QCryptographicHash& h, const ShaderRegistry::ShaderInfo& s)
{
    h.addData(s.sourcePath.toUtf8());
    h.addData(QByteArrayView("|"));
    h.addData(s.vertexShaderPath.toUtf8());
    h.addData(QByteArrayView("|"));
    h.addData(s.isUserShader ? "u" : "s");
    h.addData(QByteArrayView("|"));
    const QFileInfo fragInfo(s.sourcePath);
    h.addData(QByteArray::number(fragInfo.size()));
    h.addData(QByteArrayView("|"));
    h.addData(QByteArray::number(fragInfo.lastModified().toMSecsSinceEpoch()));
}

} // namespace

ShaderRegistry::ShaderRegistry(QObject* parent)
    : QObject(parent)
    , m_strategy(std::make_unique<ScanStrategy>(parseShader,
                                                [this]() {
                                                    Q_EMIT shadersChanged();
                                                }))
    , m_watcher(std::make_unique<PhosphorFsLoader::WatchedDirectorySet>(*m_strategy, this))
{
    qCInfo(lcShaderRegistry) << "Shader effects enabled";
    m_strategy->setPerEntryWatchPaths(shaderEntryWatchPaths);
    m_strategy->setPerDirectoryWatchPaths(shaderTopLevelWatchPaths);
    m_strategy->setPerSubdirSkip(shaderSubdirSkip);
    m_strategy->setSignatureContrib(contributeShaderSignature);
    m_strategy->setLoggingCategory(&lcShaderRegistry());
}

ShaderRegistry::~ShaderRegistry() = default;

// ═══════════════════════════════════════════════════════════════════════════════
// Search Paths
// ═══════════════════════════════════════════════════════════════════════════════

void ShaderRegistry::addSearchPath(const QString& path, PhosphorFsLoader::LiveReload liveReload)
{
    // Single-path: priority direction is irrelevant — forward with the
    // canonical default. The `addSearchPaths` overload's `order`
    // parameter only matters for multi-path batches.
    addSearchPaths(QStringList{path}, liveReload, PhosphorFsLoader::RegistrationOrder::LowestPriorityFirst);
}

void ShaderRegistry::addSearchPaths(const QStringList& paths, PhosphorFsLoader::LiveReload liveReload,
                                    PhosphorFsLoader::RegistrationOrder order)
{
    // Pre-canonicalise + drop already-registered paths via the shared
    // helper — keeps the log line below from spamming "Added search path:
    // /foo/bar/" when /foo/bar is already registered (the base's
    // `registerDirectories` is silent on dedup, so the filter has to
    // run upstream). Sister registries (`AnimationShaderRegistry`) use
    // the same helper for the same reason.
    const QStringList toRegister =
        PhosphorFsLoader::WatchedDirectorySet::filterNewSearchPaths(paths, m_watcher->directories());
    if (toRegister.isEmpty()) {
        return;
    }
    // Single batched register — the watcher runs ONE synchronous scan
    // for the whole batch, populates `m_shaders` via the strategy, and
    // emits `shadersChanged` once if the signature changed. Avoids the
    // N-rescans-on-startup amplification a loop of single-path
    // registrations would cause. The base normalises @p order into the
    // canonical scan shape before the strategy runs.
    m_watcher->registerDirectories(toRegister, liveReload, order);
    for (const QString& path : std::as_const(toRegister)) {
        qCInfo(lcShaderRegistry) << "Added search path:" << path;
    }
}

QStringList ShaderRegistry::searchPaths() const
{
    // Delegate to the watcher — it is the single source of truth.
    // Keeping a parallel `m_searchPaths` member would be one canonical
    // form away from drift (cleanPath vs raw input). Consumers that
    // pass this list to bake workers (daemon's include-path resolution)
    // see the same list the strategy was given.
    return m_watcher->directories();
}

void ShaderRegistry::setUserShaderPath(const QString& path)
{
    if (m_userShaderPath == path) {
        return; // idempotent
    }
    m_userShaderPath = path;
    m_strategy->setUserPath(path);
    // If search paths have already been registered, the prior scan baked
    // in the OLD user-path classification — synchronous rescan refreshes
    // every shader's `isUserShader` flag against the new value.
    if (m_watcher && !m_watcher->directories().isEmpty()) {
        m_watcher->rescanNow();
    }
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

void ShaderRegistry::refresh()
{
    // Synchronous rescan — re-walks every search path on the calling
    // stack, replaces the strategy's pack map, and emits `shadersChanged`
    // if the post-rescan signature differs from the pre-rescan signature.
    qCDebug(lcShaderRegistry) << "Refreshing shader registry";
    m_watcher->rescanNow();
    qCInfo(lcShaderRegistry) << "Total shaders=" << m_strategy->size();
}

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
    // Strategy returns a sorted-by-id snapshot — single source of truth
    // for QHash-randomisation-stable output across process launches.
    return m_strategy->packs();
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
    return m_strategy->pack(id);
}

QVariantMap ShaderRegistry::shaderInfo(const QString& id) const
{
    if (!m_strategy->contains(id)) {
        return QVariantMap();
    }
    return shaderInfoToVariantMap(m_strategy->pack(id));
}

QUrl ShaderRegistry::shaderUrl(const QString& id) const
{
    if (isNoneShader(id) || !m_strategy->contains(id)) {
        return QUrl();
    }
    return m_strategy->pack(id).shaderUrl;
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
// Wallpaper Path Resolution
// ═══════════════════════════════════════════════════════════════════════════════

std::unique_ptr<IWallpaperProvider> ShaderRegistry::s_wallpaperProvider;
QString ShaderRegistry::s_cachedWallpaperPath;
QImage ShaderRegistry::s_cachedWallpaperImage;
qint64 ShaderRegistry::s_cachedWallpaperMtime = 0;
QMutex ShaderRegistry::s_wallpaperCacheMutex;
std::array<ShaderRegistry::WallpaperCropEntry, ShaderRegistry::CropCacheCapacity>
    ShaderRegistry::s_cachedWallpaperCrops;
int ShaderRegistry::s_cachedWallpaperCropNextSlot = 0;

QString ShaderRegistry::wallpaperPath()
{
    QMutexLocker lock(&s_wallpaperCacheMutex);

    if (!s_cachedWallpaperPath.isEmpty() && QFile::exists(s_cachedWallpaperPath)) {
        return s_cachedWallpaperPath;
    }

    if (!s_wallpaperProvider) {
        s_wallpaperProvider = createWallpaperProvider();
    }

    s_cachedWallpaperPath = s_wallpaperProvider->wallpaperPath();
    return s_cachedWallpaperPath;
}

QImage ShaderRegistry::loadWallpaperImage()
{
    QMutexLocker lock(&s_wallpaperCacheMutex);

    // Inline path resolution (avoid calling wallpaperPath which also locks)
    QString path = s_cachedWallpaperPath;
    if (path.isEmpty() || !QFile::exists(path)) {
        lock.unlock();
        wallpaperPath(); // populates s_cachedWallpaperPath (acquires lock internally)
        lock.relock();
        path = s_cachedWallpaperPath; // re-read after relock — local copy may be stale
    }
    if (path.isEmpty()) {
        return {};
    }
    // Check if cached image is still valid (same path + same mtime)
    const QFileInfo fi(path);
    const qint64 mtime = fi.lastModified().toMSecsSinceEpoch();
    if (!s_cachedWallpaperImage.isNull() && s_cachedWallpaperMtime == mtime) {
        return s_cachedWallpaperImage;
    }
    // Load outside of lock scope is not possible since we write to static cache
    QImage img(path);
    if (img.isNull()) {
        return {};
    }
    s_cachedWallpaperImage = img.convertToFormat(QImage::Format_RGBA8888);
    s_cachedWallpaperMtime = mtime;
    qCDebug(lcShaderRegistry) << "Loaded and cached wallpaper image:" << path << s_cachedWallpaperImage.size();
    return s_cachedWallpaperImage;
}

QRect ShaderRegistry::computeWallpaperCropRect(QSize wpSize, const QRect& physGeom, const QRect& subGeom)
{
    if (wpSize.isEmpty() || !subGeom.isValid() || !physGeom.isValid() || subGeom == physGeom) {
        return {};
    }
    // Only crop when the sub-region actually lies inside the physical screen.
    const QRect clamped = subGeom.intersected(physGeom);
    if (!clamped.isValid() || clamped == physGeom) {
        return {};
    }

    // "Cover" placement of the wallpaper on the physical screen: aspect-correct
    // fill centered, overflow cropped. Mirrors shaders/wallpaper.glsl::wallpaperUv
    // so the cropped image sampled with aspect == subGeom reproduces the same
    // portion of the wallpaper that would appear inside subGeom if the whole
    // physical screen were drawn with the full wallpaper.
    const qreal wpW = wpSize.width();
    const qreal wpH = wpSize.height();
    const qreal physW = physGeom.width();
    const qreal physH = physGeom.height();
    const qreal wpAspect = wpW / qMax<qreal>(wpH, 1.0);
    const qreal physAspect = physW / qMax<qreal>(physH, 1.0);

    qreal coverX, coverY, coverW, coverH;
    if (wpAspect > physAspect) {
        coverH = wpH;
        coverW = wpH * physAspect;
        coverX = (wpW - coverW) * 0.5;
        coverY = 0.0;
    } else {
        coverW = wpW;
        coverH = wpW / qMax<qreal>(physAspect, 1.0);
        coverX = 0.0;
        coverY = (wpH - coverH) * 0.5;
    }

    // Compute edges (left/top/right/bottom) independently so adjacent VSes
    // tile the wallpaper seam-free: VS-A's right edge equals VS-B's left edge
    // because both are derived from the same coverX + frac*coverW expression
    // on the shared boundary. Deriving width from (right - left) then keeps
    // them tight regardless of how qRound breaks the tie.
    const qreal fracL = (clamped.x() - physGeom.x()) / physW;
    const qreal fracT = (clamped.y() - physGeom.y()) / physH;
    const qreal fracR = (clamped.x() + clamped.width() - physGeom.x()) / physW;
    const qreal fracB = (clamped.y() + clamped.height() - physGeom.y()) / physH;

    const int left = qRound(coverX + fracL * coverW);
    const int top = qRound(coverY + fracT * coverH);
    const int right = qRound(coverX + fracR * coverW);
    const int bottom = qRound(coverY + fracB * coverH);

    const QRect cropRect(left, top, qMax(1, right - left), qMax(1, bottom - top));
    const QRect safe = cropRect.intersected(QRect(QPoint(0, 0), wpSize));
    if (!safe.isValid() || safe.width() < 1 || safe.height() < 1) {
        return {};
    }
    return safe;
}

QImage ShaderRegistry::loadWallpaperImage(const QRect& subGeom, const QRect& physGeom)
{
    QImage full = loadWallpaperImage();
    if (full.isNull() || !subGeom.isValid() || !physGeom.isValid() || subGeom == physGeom) {
        return full;
    }

    QMutexLocker lock(&s_wallpaperCacheMutex);
    // Crops are only valid while the full wallpaper behind them is unchanged.
    // Snapshot the mtime and verify cacheKey() matches the currently-cached
    // full image — if another thread reloaded the wallpaper between our
    // loadWallpaperImage() return and this lock, our `full` is stale relative
    // to s_cachedWallpaperMtime and we must neither read from nor write to
    // the crop cache under that mtime.
    const qint64 mtime = s_cachedWallpaperMtime;
    const bool cacheConsistent = (full.cacheKey() == s_cachedWallpaperImage.cacheKey());

    if (cacheConsistent) {
        // Cache hit: return the stored QImage (stable cacheKey() so downstream
        // setWallpaperTexture equality checks short-circuit correctly).
        for (const auto& entry : s_cachedWallpaperCrops) {
            if (entry.mtime == mtime && !entry.img.isNull() && entry.sub == subGeom && entry.phys == physGeom) {
                return entry.img;
            }
        }
    }

    const QRect safe = computeWallpaperCropRect(full.size(), physGeom, subGeom);
    if (!safe.isValid()) {
        return full;
    }

    QImage cropped = full.copy(safe);
    if (cropped.isNull()) {
        return full;
    }

    if (cacheConsistent) {
        // Insert into the ring-buffer cache. Small fixed capacity — typical
        // systems have at most a handful of VSes so an LRU isn't worth the
        // bookkeeping; oldest entry is overwritten.
        s_cachedWallpaperCrops[s_cachedWallpaperCropNextSlot] = {subGeom, physGeom, mtime, cropped};
        s_cachedWallpaperCropNextSlot = (s_cachedWallpaperCropNextSlot + 1) % CropCacheCapacity;
    }
    return cropped;
}

void ShaderRegistry::invalidateWallpaperCache()
{
    QMutexLocker lock(&s_wallpaperCacheMutex);
    s_cachedWallpaperPath.clear();
    s_cachedWallpaperImage = QImage();
    s_cachedWallpaperMtime = 0;
    for (auto& entry : s_cachedWallpaperCrops) {
        entry = {};
    }
    s_cachedWallpaperCropNextSlot = 0;
    s_wallpaperProvider.reset(); // force re-detection on next call
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

        // Check if stored params has this parameter (by ID)
        if (storedParams.contains(param.id)) {
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

        // For image parameters: resolve relative paths against shader directory, emit wrap mode
        if (param.type == QLatin1String("image") && !uName.isEmpty()) {
            const QString imgPath = result.value(uName).toString();
            if (!imgPath.isEmpty() && QFileInfo(imgPath).isRelative()) {
                const QDir shaderDir = QFileInfo(info.sourcePath).absoluteDir();
                const QString resolved = shaderDir.absoluteFilePath(imgPath);
                if (QFile::exists(resolved)) {
                    result[uName] = resolved;
                }
            }
            // Ensure image params are never null QVariant
            if (!result.value(uName).isValid() || result.value(uName).isNull()) {
                result[uName] = QString();
            }
            result[uName + QStringLiteral("_wrap")] = param.wrap.isEmpty() ? QStringLiteral("clamp") : param.wrap;

            // Pass through SVG render size if present in stored params
            const QString svgSizeKey = param.id + QStringLiteral("_svgSize");
            if (storedParams.contains(svgSizeKey)) {
                result[uName + QStringLiteral("_svgSize")] = storedParams.value(svgSizeKey);
            }
        }
    }

    return result;
}

} // namespace PhosphorShaders
