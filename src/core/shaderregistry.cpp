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
        // Color slots 0-15 → customColor1-16
        if (slot >= 0 && slot < 16) {
            return QString::fromLatin1(UniformColorNames[slot]);
        }
        return QString();
    }

    // Image slots 0-3 → uTexture0-3
    if (type == QLatin1String("image")) {
        if (slot >= 0 && slot < 4) {
            return QStringLiteral("uTexture%1").arg(slot);
        }
        return QString();
    }

    // Float/int/bool slots 0-31 → customParams1_x through customParams8_w
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
            qCInfo(lcCore) << "Created user shader directory:" << dir.absolutePath();
        } else {
            qCWarning(lcCore) << "Failed to create user shader directory:" << dir.absolutePath();
        }
    }
}

void ShaderRegistry::setupFileWatcher()
{
    m_watcher = new QFileSystemWatcher(this);

    const QString sysDir = systemShaderDir();
    if (QDir(sysDir).exists()) {
        m_watcher->addPath(sysDir);
        // Watch subdirectories too — QFileSystemWatcher only fires for direct children
        QDir sysDirObj(sysDir);
        for (const QString& entry : sysDirObj.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            m_watcher->addPath(sysDirObj.filePath(entry));
        }
        qCDebug(lcCore) << "Watching system shader directory=" << sysDir;
    }

    const QString userDir = userShaderDir();
    if (QDir(userDir).exists()) {
        m_watcher->addPath(userDir);
        // Watch subdirectories for individual shader changes
        QDir userDirObj(userDir);
        for (const QString& entry : userDirObj.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            m_watcher->addPath(userDirObj.filePath(entry));
        }
        qCDebug(lcCore) << "Watching user shader directory=" << userDir;
    }

    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &ShaderRegistry::onUserShaderDirChanged);
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, &ShaderRegistry::onUserShaderDirChanged);
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
    qCInfo(lcCore) << "Shader directory changed, refreshing...";
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

    qCInfo(lcCore) << "Total shaders=" << m_shaders.size();
    Q_EMIT shadersChanged();
}

void ShaderRegistry::reportShaderBakeStarted(const QString& shaderId)
{
    Q_EMIT shaderCompilationStarted(shaderId);
}

void ShaderRegistry::reportShaderBakeFinished(const QString& shaderId, bool success, const QString& error)
{
    Q_EMIT shaderCompilationFinished(shaderId, success, error);
}

void ShaderRegistry::loadSystemShaders()
{
    // locateAll() returns paths in priority order: user first, system last
    // We reverse to load system first, so user shaders with same ID can override
    QStringList allDirs = QStandardPaths::locateAll(
        QStandardPaths::GenericDataLocation, QStringLiteral("plasmazones/shaders"), QStandardPaths::LocateDirectory);

    if (allDirs.isEmpty()) {
        qCInfo(lcCore) << "No system shader directories found";
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

        qCInfo(lcCore) << "Loaded shaders=" << (m_shaders.size() - beforeCount) << "from=" << shaderDir;
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
    const int beforeCount = m_shaders.size();

    for (const QString& entry : entries) {
        if (entry == QLatin1String("none")) {
            continue; // Skip "none"
        }
        loadShaderFromDir(dir.filePath(entry), true);
    }

    if (m_shaders.size() != beforeCount) {
        qCInfo(lcCore) << "Loaded shaders=" << (m_shaders.size() - beforeCount) << "from=" << userDir;
    }
}

void ShaderRegistry::loadShaderFromDir(const QString& shaderDir, bool isUserShader)
{
    QDir dir(shaderDir);
    const QString metadataPath = dir.filePath(QStringLiteral("metadata.json"));

    // Metadata is required
    if (!QFile::exists(metadataPath)) {
        qCDebug(lcCore) << "Skipping shader path=" << shaderDir << "reason=no metadata.json";
        return;
    }

    ShaderInfo info = loadShaderMetadata(shaderDir);
    info.isUserShader = isUserShader;

    // Multipass requires at least one buffer shader; treat missing as load failure (same as missing effect.frag)
    if (info.isMultipass && info.bufferShaderPaths.isEmpty()) {
        qCWarning(lcCore) << "Skipping multipass shader (missing buffer shader(s)):" << shaderDir;
        return;
    }

    // Validate fragment shader exists
    if (!QFile::exists(info.sourcePath)) {
        qCWarning(lcCore) << "Shader missing fragment shader:" << info.sourcePath;
        return;
    }

    // shaderUrl points directly to the raw GLSL fragment shader
    info.shaderUrl = QUrl::fromLocalFile(info.sourcePath);

    qCDebug(lcCore) << "Loaded shader name=" << info.name << "id=" << info.id
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
    info.category = root.value(QLatin1String("category")).toString();

    // Get fragment/vertex shader paths (default: effect.frag, zone.vert)
    const QString fragShaderName = root.value(QLatin1String("fragmentShader")).toString(QStringLiteral("effect.frag"));
    const QString vertShaderName = root.value(QLatin1String("vertexShader")).toString(QStringLiteral("zone.vert"));
    info.sourcePath = dir.filePath(fragShaderName);
    info.vertexShaderPath = dir.filePath(vertShaderName);

    // Multi-pass: one or more buffer pass shaders (A→B→C→D)
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
        info.bufferShaderPath = dir.filePath(bufferShaderName);
        info.bufferShaderPaths.append(info.bufferShaderPath);
    } else {
        info.bufferShaderPath = info.bufferShaderPaths.constFirst();
    }
    if (info.isMultipass) {
        bool allExist = true;
        for (const QString& path : info.bufferShaderPaths) {
            if (!QFile::exists(path)) {
                qCWarning(lcCore) << "Multipass shader missing buffer shader:" << path;
                allExist = false;
                break;
            }
        }
        if (!allExist) {
            info.bufferShaderPaths.clear();
            info.bufferShaderPath.clear();
        }
    }
    // Desktop wallpaper subscription: shader opts in to receive wallpaper at binding 11
    info.useWallpaper = root.value(QLatin1String("wallpaper")).toBool(false);

    info.bufferFeedback = root.value(QLatin1String("bufferFeedback")).toBool(false);
    qreal scale = root.value(QLatin1String("bufferScale")).toDouble(1.0);
    info.bufferScale = qBound(0.125, scale, 1.0);
    const QString wrap = root.value(QLatin1String("bufferWrap")).toString(QStringLiteral("clamp"));
    info.bufferWrap = (wrap == QLatin1String("repeat")) ? QStringLiteral("repeat") : QStringLiteral("clamp");

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

} // namespace PlasmaZones
