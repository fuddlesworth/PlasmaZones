// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shaderregistry.h"

#include <QColor>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>

#include <algorithm>

#include "logging.h"

namespace PlasmaZones {

// Namespace UUID for generating deterministic shader IDs (UUID v5)
static const QUuid ShaderNamespaceUuid = QUuid::fromString(QStringLiteral("{a1b2c3d4-e5f6-4a5b-8c9d-0e1f2a3b4c5d}"));

// Uniform name components for slot mapping
static const char* const UniformVecNames[] = {"customParams1", "customParams2", "customParams3", "customParams4"};
static const char* const UniformComponents[] = {"_x", "_y", "_z", "_w"};
static const char* const UniformColorNames[] = {"customColor1", "customColor2", "customColor3", "customColor4",
                                                "customColor5", "customColor6", "customColor7", "customColor8"};

QString ShaderRegistry::ParameterInfo::uniformName() const
{
    if (slot < 0) {
        return QString();
    }

    if (type == QLatin1String("color")) {
        // Color slots 0-7 → customColor1-8
        if (slot >= 0 && slot < 8) {
            return QString::fromLatin1(UniformColorNames[slot]);
        }
        return QString();
    }

    // Float/int/bool slots 0-15 → customParams1_x through customParams4_w
    if (slot >= 0 && slot < 16) {
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
    return QUuid::createUuidV5(ShaderNamespaceUuid, name).toString(QUuid::WithBraces);
}

ShaderRegistry* ShaderRegistry::s_instance = nullptr;

ShaderRegistry::ShaderRegistry(QObject* parent)
    : QObject(parent)
{
    // This gets created during Daemon::init() before anything else touches it,
    // so we're safe. If we ever need multi-threaded access, slap a mutex on this.
    s_instance = this;

#ifdef PLASMAZONES_SHADERS_ENABLED
    m_shadersEnabled = true;
    qCInfo(lcCore) << "Shader effects enabled (Qt6::ShaderTools available at build time)";
#else
    m_shadersEnabled = false;
    qCInfo(lcCore) << "Shader effects disabled (Qt6::ShaderTools not available at build time)";
#endif

    if (m_shadersEnabled) {
        ensureUserShaderDirExists();
        setupFileWatcher();
        refresh();
    }
    // No "no effect" placeholder needed - there's a toggle to disable shaders
}

ShaderRegistry::~ShaderRegistry()
{
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

ShaderRegistry* ShaderRegistry::instance()
{
    return s_instance;
}

QString ShaderRegistry::noneShaderUuid()
{
    // Empty string means "no shader" - keeps things simple
    return QString();
}

bool ShaderRegistry::isNoneShader(const QString& id)
{
    return id.isEmpty();
}

QString ShaderRegistry::systemShaderDir()
{
    // System shaders installed by package
    return QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("plasmazones/shaders"),
                                  QStandardPaths::LocateDirectory);
}

QString ShaderRegistry::userShaderDir()
{
    // User shaders in ~/.local/share/plasmazones/shaders
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/plasmazones/shaders");
}

void ShaderRegistry::ensureUserShaderDirExists() const
{
    QDir dir(userShaderDir());
    if (!dir.exists()) {
        if (dir.mkpath(QStringLiteral("."))) {
            qCDebug(lcCore) << "Created user shader directory:" << dir.absolutePath();
        } else {
            qCWarning(lcCore) << "Failed to create user shader directory:" << dir.absolutePath();
        }
    }
}

void ShaderRegistry::setupFileWatcher()
{
    m_watcher = new QFileSystemWatcher(this);

    const QString userDir = userShaderDir();
    if (QDir(userDir).exists()) {
        m_watcher->addPath(userDir);
        qCDebug(lcCore) << "Watching user shader directory:" << userDir;
    }

    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &ShaderRegistry::onUserShaderDirChanged);
}

void ShaderRegistry::onUserShaderDirChanged(const QString& path)
{
    Q_UNUSED(path)

    // Debounce rapid changes (e.g., editor auto-save)
    if (!m_refreshTimer) {
        m_refreshTimer = new QTimer(this);
        m_refreshTimer->setSingleShot(true);
        m_refreshTimer->setInterval(RefreshDebounceMs);
        connect(m_refreshTimer, &QTimer::timeout, this, &ShaderRegistry::performDebouncedRefresh);
    }

    m_refreshTimer->start();
}

void ShaderRegistry::performDebouncedRefresh()
{
    qCDebug(lcCore) << "User shader directory changed, refreshing...";
    refresh();
}

void ShaderRegistry::refresh()
{
    qCDebug(lcCore) << "Refreshing shader registry";

    m_shaders.clear();

    if (m_shadersEnabled) {
        // Load order matters: system shaders first, then user shaders
        // User shaders with same ID will override system shaders
        loadSystemShaders();
        loadUserShaders();
    }

    qCInfo(lcCore) << "Loaded" << m_shaders.size() << "shaders";
    Q_EMIT shadersChanged();
}

void ShaderRegistry::loadSystemShaders()
{
    // locateAll() returns paths in priority order: user first, system last
    // We reverse to load system first, so user shaders with same ID can override
    QStringList allDirs = QStandardPaths::locateAll(
        QStandardPaths::GenericDataLocation, QStringLiteral("plasmazones/shaders"), QStandardPaths::LocateDirectory);

    if (allDirs.isEmpty()) {
        qCDebug(lcCore) << "No system shader directories found";
        return;
    }

    // Reverse order: load system shaders first, user shaders last (override)
    std::reverse(allDirs.begin(), allDirs.end());

    for (const QString& shaderDir : allDirs) {
        QDir dir(shaderDir);
        if (!dir.exists()) {
            continue;
        }

        const auto entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        const int beforeCount = m_shaders.size();

        for (const QString& entry : entries) {
            if (entry == QLatin1String("none")) {
                continue; // Skip "none" - already added
            }
            loadShaderFromDir(dir.filePath(entry), false);
        }

        qCDebug(lcCore) << "Loaded" << (m_shaders.size() - beforeCount) << "shaders from:" << shaderDir;
    }
}

void ShaderRegistry::loadUserShaders()
{
    const QString userDir = userShaderDir();
    if (!QDir(userDir).exists()) {
        return;
    }

    QDir dir(userDir);
    const auto entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QString& entry : entries) {
        if (entry == QLatin1String("none")) {
            continue; // Skip "none"
        }
        loadShaderFromDir(dir.filePath(entry), true);
    }
}

void ShaderRegistry::loadShaderFromDir(const QString& shaderDir, bool isUserShader)
{
    QDir dir(shaderDir);
    const QString metadataPath = dir.filePath(QStringLiteral("metadata.json"));

    // Metadata is required
    if (!QFile::exists(metadataPath)) {
        qCDebug(lcCore) << "Skipping shader (no metadata.json):" << shaderDir;
        return;
    }

    ShaderInfo info = loadShaderMetadata(shaderDir);
    info.isUserShader = isUserShader;

    // Validate fragment shader exists
    if (!QFile::exists(info.sourcePath)) {
        qCWarning(lcCore) << "Shader missing fragment shader:" << info.sourcePath;
        return;
    }

    // shaderUrl points directly to the raw GLSL fragment shader
    info.shaderUrl = QUrl::fromLocalFile(info.sourcePath);

    qCDebug(lcCore) << "  Shader:" << info.name << "[" << info.id << "]" << (isUserShader ? "(user)" : "(system)");

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
        qCWarning(lcCore) << "Failed to parse shader metadata:" << metadataPath << error.errorString();
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

    // Get fragment/vertex shader paths (default: effect.glsl, zone.vert.glsl)
    const QString fragShaderName = root.value(QLatin1String("fragmentShader")).toString(QStringLiteral("effect.glsl"));
    const QString vertShaderName = root.value(QLatin1String("vertexShader")).toString(QStringLiteral("zone.vert.glsl"));
    info.sourcePath = dir.filePath(fragShaderName);
    info.vertexShaderPath = dir.filePath(vertShaderName);

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

        if (!param.id.isEmpty()) {
            info.parameters.append(param);
        }
    }

    return info;
}

QList<ShaderRegistry::ShaderInfo> ShaderRegistry::availableShaders() const
{
    return m_shaders.values();
}

QVariantList ShaderRegistry::availableShadersVariant() const
{
    QVariantList result;
    for (const ShaderInfo& info : m_shaders) {
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
    map[QStringLiteral("isValid")] = info.isValid();

    // Optional fields - only include if non-empty (avoids D-Bus issues with empty URLs)
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

    // Parameters list (empty list is OK for D-Bus)
    QVariantList params;
    for (const ParameterInfo& param : info.parameters) {
        params.append(parameterInfoToVariantMap(param));
    }
    map[QStringLiteral("parameters")] = params;

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

    // Only include optional values if they are valid (D-Bus can't marshal null QVariants)
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

QUrl ShaderRegistry::shaderUrl(const QString& id) const
{
    if (isNoneShader(id) || !m_shaders.contains(id)) {
        return QUrl();
    }
    return m_shaders.value(id).shaderUrl;
}

bool ShaderRegistry::shadersEnabled() const
{
    return m_shadersEnabled;
}

bool ShaderRegistry::userShadersEnabled() const
{
    // User shaders just need GLSL files (no compilation needed for RenderNode)
    return m_shadersEnabled;
}

QString ShaderRegistry::userShaderDirectory() const
{
    return userShaderDir();
}

void ShaderRegistry::openUserShaderDirectory() const
{
    ensureUserShaderDirExists();
    QDesktopServices::openUrl(QUrl::fromLocalFile(userShaderDir()));
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
                qCWarning(lcCore) << "Invalid shader parameter:" << param.id << "for shader:" << id;
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
        const QString uniformName = param.uniformName();
        if (uniformName.isEmpty()) {
            continue; // Parameter doesn't map to a uniform
        }

        // Check if stored params has this parameter (by ID)
        if (storedParams.contains(param.id)) {
            QVariant value = storedParams.value(param.id);

            // Handle color type - convert to Qt color object if it's a string
            if (param.type == QLatin1String("color") && value.typeId() == QMetaType::QString) {
                QColor color(value.toString());
                if (color.isValid()) {
                    result[uniformName] = QVariant::fromValue(color);
                } else {
                    result[uniformName] = param.defaultValue;
                }
            } else {
                result[uniformName] = value;
            }
        } else {
            // Use default value for missing parameters
            if (param.type == QLatin1String("color")) {
                // Ensure color default is a QColor object
                QColor color(param.defaultValue.toString());
                result[uniformName] = color.isValid() ? QVariant::fromValue(color) : param.defaultValue;
            } else {
                result[uniformName] = param.defaultValue;
            }
        }
    }

    return result;
}

} // namespace PlasmaZones
