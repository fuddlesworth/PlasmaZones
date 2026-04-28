// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShell/ShaderRegistry.h>
#include <PhosphorShell/IWallpaperProvider.h>
#include "shaderutils.h"

#include <PhosphorFsLoader/IScanStrategy.h>
#include <PhosphorFsLoader/WatchedDirectorySet.h>

#include <QColor>
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

namespace PhosphorShell {

Q_LOGGING_CATEGORY(lcShaderRegistry, "phosphorshell.shaderregistry")

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

/// Scan strategy backing the shader registry's `WatchedDirectorySet`.
/// Walks subdirectories of every registered search path, parses
/// `metadata.json` per shader, and reports the file/subdir paths that
/// the base must re-arm individual watches on after each rescan.
///
/// The base's `WatchedDirectorySet::directories()` is the single source
/// of truth for the registered search paths — `searchPaths()` forwards
/// to it directly so callers always see the canonicalised, deduplicated
/// list (no second member to keep in lockstep).
class ShaderRegistry::ShaderScanStrategy : public PhosphorFsLoader::IScanStrategy
{
public:
    explicit ShaderScanStrategy(ShaderRegistry& reg)
        : m_reg(&reg)
    {
    }

    QStringList performScan(const QStringList& directoriesInScanOrder) override
    {
        return m_reg->performScan(directoriesInScanOrder);
    }

private:
    ShaderRegistry* m_reg;
};

ShaderRegistry::ShaderRegistry(QObject* parent)
    : QObject(parent)
{
    qCInfo(lcShaderRegistry) << "Shader effects enabled";

    m_strategy = std::make_unique<ShaderScanStrategy>(*this);
    m_watcher = std::make_unique<PhosphorFsLoader::WatchedDirectorySet>(*m_strategy, this);
    // Every rescan emits `shadersChanged` unconditionally — matches the
    // legacy behaviour of `refresh()` always emitting on completion.
    // Consumers that want change-only semantics layer their own diff on
    // top.
    connect(m_watcher.get(), &PhosphorFsLoader::WatchedDirectorySet::rescanCompleted, this,
            &ShaderRegistry::shadersChanged);
}

ShaderRegistry::~ShaderRegistry() = default;

// ═══════════════════════════════════════════════════════════════════════════════
// Search Paths
// ═══════════════════════════════════════════════════════════════════════════════

void ShaderRegistry::addSearchPath(const QString& path)
{
    addSearchPaths(QStringList{path});
}

void ShaderRegistry::addSearchPaths(const QStringList& paths)
{
    // Filter empties and pre-canonicalise so dedup matches what the
    // watcher will store internally — `WatchedDirectorySet` canonicalises
    // via `QDir::cleanPath` on insertion, so two callers spelling the
    // same path with and without a trailing slash collapse to one
    // entry. Doing the same dedup HERE means we don't log
    // "Added search path: /foo/bar/" when /foo/bar is already
    // registered.
    const QStringList alreadyRegistered = m_watcher->directories();
    QStringList toRegister;
    toRegister.reserve(paths.size());
    for (const QString& path : paths) {
        if (path.isEmpty()) {
            continue;
        }
        const QString canonical = QDir::cleanPath(path);
        if (alreadyRegistered.contains(canonical) || toRegister.contains(canonical)) {
            continue;
        }
        toRegister.append(canonical);
    }
    if (toRegister.isEmpty()) {
        return;
    }
    // Single batched register — the watcher runs ONE synchronous scan
    // for the whole batch, populates `m_shaders` via the strategy, and
    // emits `shadersChanged` once. Avoids the N-rescans-on-startup
    // amplification that a loop of single-path registrations would cause.
    m_watcher->registerDirectories(toRegister, PhosphorFsLoader::LiveReload::On);
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
    // Canonicalisation happens at compare time in `performScan` (so
    // callers that pass a path which doesn't exist yet still get the
    // right classification once it materialises). Store the raw input.
    m_userShaderPath = path;
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
    // Synchronous rescan — `m_watcher->rescanNow()` calls into
    // `ShaderScanStrategy::performScan` on this stack, which clears and
    // reloads `m_shaders` and returns the full watch list. The
    // `rescanCompleted` signal connected in the constructor emits
    // `shadersChanged` after the strategy returns.
    qCDebug(lcShaderRegistry) << "Refreshing shader registry";
    m_watcher->rescanNow();
    qCInfo(lcShaderRegistry) << "Total shaders=" << m_shaders.size();
}

QStringList ShaderRegistry::performScan(const QStringList& directoriesInScanOrder)
{
    m_shaders.clear();
    QStringList desiredWatches;

    // Resolve the user-shader path's canonical form ONCE per rescan
    // (empty when no user path is configured or the configured path
    // doesn't exist yet). Each iterated dir is canonicalised below and
    // compared against this — match means the dir is the user path,
    // and discovered shaders are flagged `isUserShader = true`.
    const QString canonicalUserPath =
        m_userShaderPath.isEmpty() ? QString() : QFileInfo(m_userShaderPath).canonicalFilePath();

    // Reverse-iterate with first-registration-wins, matching the
    // IScanStrategy convention used by `JsonScanStrategy` and
    // `JsScanStrategy`. Caller registers dirs in
    // `[system-lowest, ..., system-highest, user]` order; reversing
    // here lets the user dir claim its shader IDs before the system
    // dirs are touched, which yields the canonical XDG semantic
    // `user > sys-highest > sys-mid > sys-lowest`.
    //
    // Single-pass enumeration: each subdir is dispatched to
    // `loadShaderFromDir` and harvested for the per-rescan watch list
    // in the same loop. The base re-arms `desiredWatches` every rescan
    // to compensate for `cmake --install`'s delete+recreate inode
    // churn.
    for (auto dirIt = directoriesInScanOrder.crbegin(); dirIt != directoriesInScanOrder.crend(); ++dirIt) {
        const QString& searchPath = *dirIt;
        QDir dirObj(searchPath);
        if (!dirObj.exists()) {
            qCDebug(lcShaderRegistry) << "Search path does not exist:" << searchPath;
            continue;
        }

        // Classify the iterated dir as user vs system. Empty
        // `canonicalUserPath` (no user path configured, or user dir
        // doesn't exist yet) yields `false` for every dir — preserving
        // the legacy default before this knob existed.
        const bool isUserDir =
            !canonicalUserPath.isEmpty() && QFileInfo(searchPath).canonicalFilePath() == canonicalUserPath;

        const int beforeCount = m_shaders.size();
        const QStringList subdirs = dirObj.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString& subdir : subdirs) {
            if (subdir == QLatin1String("none")) {
                continue; // reserved sentinel for "no shader"
            }
            const QString subPath = dirObj.filePath(subdir);
            // Dispatch + watch-list harvest in one pass.
            // `loadShaderFromDir` first-wins-skips on existing IDs (see
            // its early-return when `m_shaders.contains`).
            loadShaderFromDir(subPath, isUserDir);
            desiredWatches.append(subPath);
            QDir sub(subPath);
            const QStringList shaderFiles = sub.entryList({QStringLiteral("*.frag"), QStringLiteral("*.vert"),
                                                           QStringLiteral("*.glsl"), QStringLiteral("*.json")},
                                                          QDir::Files);
            for (const QString& file : shaderFiles) {
                desiredWatches.append(sub.filePath(file));
            }
        }
        // Top-level shared includes (common.glsl, audio.glsl, etc.).
        const QStringList topFiles =
            dirObj.entryList({QStringLiteral("*.glsl"), QStringLiteral("*.json")}, QDir::Files);
        for (const QString& file : topFiles) {
            desiredWatches.append(dirObj.filePath(file));
        }
        qCInfo(lcShaderRegistry) << "Loaded shaders=" << (m_shaders.size() - beforeCount) << "from=" << searchPath;
    }

    return desiredWatches;
}

void ShaderRegistry::reportShaderBakeStarted(const QString& shaderId)
{
    Q_EMIT shaderCompilationStarted(shaderId);
}

void ShaderRegistry::reportShaderBakeFinished(const QString& shaderId, bool success, const QString& error)
{
    Q_EMIT shaderCompilationFinished(shaderId, success, error);
}

void ShaderRegistry::loadShaderFromDir(const QString& shaderDir, bool isUserShader)
{
    QDir dir(shaderDir);
    const QString metadataPath = dir.filePath(QStringLiteral("metadata.json"));

    // Metadata is required
    if (!QFile::exists(metadataPath)) {
        qCDebug(lcShaderRegistry) << "Skipping shader path=" << shaderDir << "reason=no metadata.json";
        return;
    }

    ShaderInfo info = loadShaderMetadata(shaderDir);
    info.isUserShader = isUserShader;

    // First-registration-wins. `performScan` reverse-iterates so the
    // user dir is processed first; a subsequent system shader with a
    // colliding id is shadowed and silently skipped here.
    if (m_shaders.contains(info.id)) {
        qCDebug(lcShaderRegistry) << "Shader id=" << info.id << "already registered from a higher-priority dir; "
                                  << "shadowed at=" << shaderDir;
        return;
    }

    // Multipass requires at least one buffer shader; treat missing as load failure
    if (info.isMultipass && info.bufferShaderPaths.isEmpty()) {
        qCWarning(lcShaderRegistry) << "Skipping multipass shader (missing buffer shader(s)):" << shaderDir;
        return;
    }

    // Validate fragment shader exists
    if (!QFile::exists(info.sourcePath)) {
        qCWarning(lcShaderRegistry) << "Shader missing fragment shader:" << info.sourcePath;
        return;
    }

    // shaderUrl points directly to the raw GLSL fragment shader
    info.shaderUrl = QUrl::fromLocalFile(info.sourcePath);

    qCDebug(lcShaderRegistry) << "Loaded shader name=" << info.name << "id=" << info.id
                              << "source=" << (isUserShader ? "user" : "system") << "from=" << shaderDir;

    // Check for preview image
    const QString previewPath = dir.filePath(QStringLiteral("preview.png"));
    if (QFile::exists(previewPath)) {
        info.previewPath = previewPath;
    }

    m_shaders.insert(info.id, info);
}

ShaderRegistry::ShaderInfo ShaderRegistry::loadShaderMetadata(const QString& shaderDir)
{
    ShaderInfo info;
    QDir dir(shaderDir);

    // Default name from directory name, ID is UUID generated from name
    const QString shaderName = dir.dirName();
    info.id = shaderNameToUuid(shaderName);
    info.name = shaderName; // Human-readable name defaults to directory name

    const QString metadataPath = dir.filePath(QStringLiteral("metadata.json"));
    QFile file(metadataPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return info;
    }

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError) {
        qCWarning(lcShaderRegistry) << "Failed to parse shader metadata:" << metadataPath << error.errorString();
        return info;
    }

    QJsonObject root = doc.object();

    // If metadata has an "id" field, use it to generate the UUID (for consistency)
    // Otherwise use the directory name. The "name" field is for display only.
    const QString metadataId = root.value(QLatin1String("id")).toString(shaderName);
    info.id = shaderNameToUuid(metadataId);
    info.name = root.value(QLatin1String("name")).toString(shaderName);
    info.description = root.value(QLatin1String("description")).toString();
    info.author = root.value(QLatin1String("author")).toString();
    info.version = root.value(QLatin1String("version")).toString(QStringLiteral("1.0"));
    info.category = root.value(QLatin1String("category")).toString();

    // Get fragment/vertex shader paths (default: effect.frag, zone.vert)
    const QString fragShaderName = root.value(QLatin1String("fragmentShader")).toString(QStringLiteral("effect.frag"));
    const QString vertShaderName = root.value(QLatin1String("vertexShader")).toString(QStringLiteral("zone.vert"));
    info.sourcePath = dir.filePath(fragShaderName);
    info.vertexShaderPath = dir.filePath(vertShaderName);

    // Multi-pass: one or more buffer pass shaders (A->B->C->D)
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
    // Desktop wallpaper subscription: shader opts in to receive wallpaper at binding 11
    info.useWallpaper = root.value(QLatin1String("wallpaper")).toBool(false);

    info.bufferFeedback = root.value(QLatin1String("bufferFeedback")).toBool(false);
    qreal scale = root.value(QLatin1String("bufferScale")).toDouble(1.0);
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

    // Per-channel filter modes: "nearest", "linear", or "mipmap"
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

    // Parse parameters
    const QJsonArray paramsArray = root.value(QLatin1String("parameters")).toArray();
    for (const QJsonValue& paramValue : paramsArray) {
        QJsonObject paramObj = paramValue.toObject();
        ParameterInfo param;
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

    // Parse presets (named parameter configurations)
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

// ═══════════════════════════════════════════════════════════════════════════════
// Query Methods
// ═══════════════════════════════════════════════════════════════════════════════

QList<ShaderRegistry::ShaderInfo> ShaderRegistry::availableShaders() const
{
    // Sort by id for deterministic ordering. QHash iteration order is
    // intentionally randomised in Qt6 — without the sort, downstream
    // consumers (the daemon's bake-warm loop, settings dropdowns,
    // QML model assignments) see a different shader order on every
    // process launch, which surfaces as flaky snapshot tests and
    // visible UI reordering between sessions.
    QList<ShaderInfo> sorted = m_shaders.values();
    std::sort(sorted.begin(), sorted.end(), [](const ShaderInfo& a, const ShaderInfo& b) {
        return a.id < b.id;
    });
    return sorted;
}

QVariantList ShaderRegistry::availableShadersVariant() const
{
    // Mirror availableShaders()'s sort — same rationale.
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
    return m_shaders.value(id);
}

QVariantMap ShaderRegistry::shaderInfo(const QString& id) const
{
    if (!m_shaders.contains(id)) {
        return QVariantMap();
    }
    return shaderInfoToVariantMap(m_shaders.value(id));
}

QUrl ShaderRegistry::shaderUrl(const QString& id) const
{
    if (isNoneShader(id) || !m_shaders.contains(id)) {
        return QUrl();
    }
    return m_shaders.value(id).shaderUrl;
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

} // namespace PhosphorShell
