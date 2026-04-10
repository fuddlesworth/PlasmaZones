// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "baseshaderregistry.h"
#include "logging.h"

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

QString BaseShaderRegistry::ParameterInfo::uniformName() const
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

QString BaseShaderRegistry::shaderNameToUuid(const QString& name)
{
    if (name.isEmpty()) {
        return QString();
    }
    return QUuid::createUuidV5(ShaderNamespaceUuid, name).toString();
}

BaseShaderRegistry::BaseShaderRegistry(QObject* parent)
    : QObject(parent)
{
}

BaseShaderRegistry::~BaseShaderRegistry() = default;

QString BaseShaderRegistry::systemShaderDir() const
{
    return QStandardPaths::locate(QStandardPaths::GenericDataLocation, systemDirName(),
                                  QStandardPaths::LocateDirectory);
}

QString BaseShaderRegistry::userShaderDir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/") + userDirName();
}

void BaseShaderRegistry::ensureUserShaderDirExists() const
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

void BaseShaderRegistry::watchShaderDirectory(const QString& dir, bool guardDuplicates)
{
    if (!m_watcher || !QDir(dir).exists()) {
        return;
    }

    auto addPath = [this, guardDuplicates](const QString& path, bool isDir) {
        if (guardDuplicates) {
            const auto& existing = isDir ? m_watcher->directories() : m_watcher->files();
            if (existing.contains(path))
                return;
        }
        m_watcher->addPath(path);
    };

    static const QStringList shaderFilePatterns = {QStringLiteral("*.frag"), QStringLiteral("*.vert"),
                                                   QStringLiteral("*.glsl"), QStringLiteral("*.json")};
    static const QStringList topLevelPatterns = {QStringLiteral("*.glsl"), QStringLiteral("*.json")};

    addPath(dir, true);
    QDir dirObj(dir);
    for (const QString& subdir : dirObj.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString subPath = dirObj.filePath(subdir);
        addPath(subPath, true);
        QDir sub(subPath);
        for (const QString& file : sub.entryList(shaderFilePatterns, QDir::Files)) {
            addPath(sub.filePath(file), false);
        }
    }
    for (const QString& file : dirObj.entryList(topLevelPatterns, QDir::Files)) {
        addPath(dirObj.filePath(file), false);
    }
}

void BaseShaderRegistry::watchAllShaderDirectories(bool guardDuplicates)
{
    const QStringList allDirs = QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, systemDirName(),
                                                          QStandardPaths::LocateDirectory);
    for (const QString& dir : allDirs) {
        watchShaderDirectory(dir, guardDuplicates);
    }
    const QString userDir = userShaderDir();
    if (!allDirs.contains(userDir)) {
        watchShaderDirectory(userDir, guardDuplicates);
    }
}

void BaseShaderRegistry::setupFileWatcher()
{
    m_watcher = new QFileSystemWatcher(this);

    watchAllShaderDirectories(false);

    qCInfo(lcCore) << "Watching shader directories paths="
                   << m_watcher->files().size() + m_watcher->directories().size();

    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &BaseShaderRegistry::onDirChanged);
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, &BaseShaderRegistry::onFileChanged);
}

void BaseShaderRegistry::onDirChanged(const QString& path)
{
    qCInfo(lcCore) << "Shader directory change detected:" << path;
    scheduleRefresh();
}

void BaseShaderRegistry::onFileChanged(const QString& path)
{
    qCInfo(lcCore) << "Shader file change detected:" << path;

    // QFileSystemWatcher drops the watch after atomic rename (new inode).
    if (QFile::exists(path) && m_watcher && !m_watcher->files().contains(path)) {
        m_watcher->addPath(path);
    }

    scheduleRefresh();
}

void BaseShaderRegistry::scheduleRefresh()
{
    if (!m_refreshTimer) {
        m_refreshTimer = new QTimer(this);
        m_refreshTimer->setSingleShot(true);
        m_refreshTimer->setInterval(RefreshDebounceMs);
        connect(m_refreshTimer, &QTimer::timeout, this, &BaseShaderRegistry::performDebouncedRefresh);
    }

    m_refreshTimer->start();
}

void BaseShaderRegistry::performDebouncedRefresh()
{
    qCInfo(lcCore) << "Shader directory changed, refreshing...";
    refresh();
    watchAllShaderDirectories(true);
}

void BaseShaderRegistry::refresh()
{
    qCDebug(lcCore) << "Refreshing shader registry for" << systemDirName();

    // Notify subclasses about removed shaders before clearing
    for (auto it = m_shaders.constBegin(); it != m_shaders.constEnd(); ++it) {
        onShaderRemoved(it.key());
    }

    m_shaders.clear();
    loadAllShaders();

    qCInfo(lcCore) << "Total shaders=" << m_shaders.size() << "in" << systemDirName();
    Q_EMIT shadersChanged();
}

void BaseShaderRegistry::loadAllShaders()
{
    // locateAll() returns paths in priority order: user first, system last
    // We reverse to load system first, so user shaders with same ID can override
    QStringList allDirs = QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, systemDirName(),
                                                    QStandardPaths::LocateDirectory);

    if (!allDirs.isEmpty()) {
        std::reverse(allDirs.begin(), allDirs.end());
        for (const QString& shaderDir : allDirs) {
            loadShadersFromDir(shaderDir, false);
        }
    }

    // Load user shaders last (override system)
    const QString userDir = userShaderDir();
    if (QDir(userDir).exists()) {
        loadShadersFromDir(userDir, true);
    }
}

void BaseShaderRegistry::loadShadersFromDir(const QString& dir, bool isUserShader)
{
    QDir dirObj(dir);
    if (!dirObj.exists()) {
        return;
    }

    const auto entries = dirObj.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    const int beforeCount = m_shaders.size();

    for (const QString& entry : entries) {
        if (entry == QLatin1String("none")) {
            continue;
        }
        loadShaderFromDir(dirObj.filePath(entry), isUserShader);
    }

    if (m_shaders.size() != beforeCount) {
        qCInfo(lcCore) << "Loaded shaders=" << (m_shaders.size() - beforeCount) << "from=" << dir;
    }
}

void BaseShaderRegistry::loadShaderFromDir(const QString& shaderDir, bool isUserShader)
{
    QDir dir(shaderDir);
    const QString metadataPath = dir.filePath(QStringLiteral("metadata.json"));

    if (!QFile::exists(metadataPath)) {
        qCDebug(lcCore) << "Skipping shader path=" << shaderDir << "reason=no metadata.json";
        return;
    }

    QJsonObject rootJson;
    BaseShaderInfo info = loadBaseMetadata(shaderDir, &rootJson);
    info.isUserShader = isUserShader;

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

    // Pass the already-parsed JSON to the subclass hook (no second file read)
    if (!rootJson.isEmpty()) {
        onShaderLoaded(info.id, rootJson, shaderDir, isUserShader);
    }
}

BaseShaderRegistry::BaseShaderInfo BaseShaderRegistry::loadBaseMetadata(const QString& shaderDir, QJsonObject* outRoot)
{
    BaseShaderInfo info;
    QDir dir(shaderDir);

    const QString shaderName = dir.dirName();
    info.id = shaderNameToUuid(shaderName);
    info.name = shaderName;

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
    if (outRoot)
        *outRoot = root;

    const QString metadataId = root.value(QLatin1String("id")).toString(shaderName);
    info.id = shaderNameToUuid(metadataId);
    info.name = root.value(QLatin1String("name")).toString(shaderName);
    info.description = root.value(QLatin1String("description")).toString();
    info.author = root.value(QLatin1String("author")).toString();
    info.version = root.value(QLatin1String("version")).toString(QStringLiteral("1.0"));
    info.category = root.value(QLatin1String("category")).toString();

    // Get fragment shader path (default: effect.frag)
    const QString fragShaderName = root.value(QLatin1String("fragmentShader")).toString(QStringLiteral("effect.frag"));
    info.sourcePath = dir.filePath(fragShaderName);

    // Parse parameters
    const QJsonArray paramsArray = root.value(QLatin1String("parameters")).toArray();
    for (const QJsonValue& paramValue : paramsArray) {
        ParameterInfo param = parseParameter(paramValue.toObject());
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

BaseShaderRegistry::ParameterInfo BaseShaderRegistry::parseParameter(const QJsonObject& paramObj)
{
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
    return param;
}

// ===============================================================================
// Query methods
// ===============================================================================

QVariantList BaseShaderRegistry::availableShadersVariant() const
{
    QVariantList result;
    for (const BaseShaderInfo& info : m_shaders) {
        result.append(shaderInfoToVariantMap(info));
    }
    return result;
}

QVariantMap BaseShaderRegistry::shaderInfo(const QString& id) const
{
    if (!m_shaders.contains(id)) {
        return QVariantMap();
    }
    return shaderInfoToVariantMap(m_shaders.value(id));
}

QUrl BaseShaderRegistry::shaderUrl(const QString& id) const
{
    if (id.isEmpty() || !m_shaders.contains(id)) {
        return QUrl();
    }
    return m_shaders.value(id).shaderUrl;
}

QString BaseShaderRegistry::userShaderDirectory() const
{
    return userShaderDir();
}

void BaseShaderRegistry::openUserShaderDirectory() const
{
    ensureUserShaderDirExists();
    QDesktopServices::openUrl(QUrl::fromLocalFile(userShaderDir()));
}

QVariantMap BaseShaderRegistry::defaultParams(const QString& id) const
{
    QVariantMap result;
    if (!m_shaders.contains(id)) {
        return result;
    }
    const BaseShaderInfo& info = m_shaders.value(id);
    for (const ParameterInfo& param : info.parameters) {
        result[param.id] = param.defaultValue;
    }
    return result;
}

QVariantList BaseShaderRegistry::shaderPresetsVariant(const QString& id) const
{
    if (!m_shaders.contains(id)) {
        return {};
    }
    const BaseShaderInfo& info = m_shaders.value(id);
    QVariantList result;
    for (auto it = info.presets.constBegin(); it != info.presets.constEnd(); ++it) {
        QVariantMap entry;
        entry[QLatin1String("name")] = it.key();
        entry[QLatin1String("params")] = it.value();
        result.append(entry);
    }
    return result;
}

QStringList BaseShaderRegistry::shaderPresetNames(const QString& id) const
{
    if (!m_shaders.contains(id)) {
        return {};
    }
    return m_shaders.value(id).presets.keys();
}

QVariantMap BaseShaderRegistry::presetParams(const QString& id, const QString& presetName) const
{
    if (!m_shaders.contains(id)) {
        return {};
    }
    const BaseShaderInfo& info = m_shaders.value(id);
    if (!info.presets.contains(presetName)) {
        return {};
    }
    return validateAndCoerceParams(id, info.presets.value(presetName));
}

// ===============================================================================
// Parameter validation and translation
// ===============================================================================

bool BaseShaderRegistry::validateParameterValue(const ParameterInfo& param, const QVariant& value) const
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

QVariantMap BaseShaderRegistry::validateAndCoerceParams(const QString& id, const QVariantMap& params) const
{
    QVariantMap result;
    if (!m_shaders.contains(id)) {
        return result;
    }
    const BaseShaderInfo& info = m_shaders.value(id);
    for (const ParameterInfo& param : info.parameters) {
        if (params.contains(param.id) && validateParameterValue(param, params.value(param.id))) {
            result[param.id] = params.value(param.id);
        } else {
            result[param.id] = param.defaultValue;
        }
    }
    return result;
}

QVariantMap BaseShaderRegistry::translateParamsToUniforms(const QString& id, const QVariantMap& storedParams) const
{
    QVariantMap result;
    if (id.isEmpty() || !m_shaders.contains(id)) {
        return result;
    }
    const BaseShaderInfo& info = m_shaders.value(id);

    for (const ParameterInfo& param : info.parameters) {
        const QString uniName = param.uniformName();
        if (uniName.isEmpty()) {
            continue;
        }

        if (storedParams.contains(param.id)) {
            QVariant value = storedParams.value(param.id);

            if (param.type == QLatin1String("color") && value.typeId() == QMetaType::QString) {
                QColor color(value.toString());
                if (color.isValid()) {
                    result[uniName] = color.name(QColor::HexArgb);
                } else {
                    QColor defColor(param.defaultValue.toString());
                    result[uniName] = defColor.isValid() ? defColor.name(QColor::HexArgb) : QStringLiteral("#00000000");
                }
            } else {
                result[uniName] = value;
            }
        } else {
            if (param.type == QLatin1String("color")) {
                QColor color(param.defaultValue.toString());
                result[uniName] = color.isValid() ? color.name(QColor::HexArgb) : QStringLiteral("#00000000");
            } else if (!param.defaultValue.isValid() || param.defaultValue.isNull()) {
                if (param.type == QLatin1String("image")) {
                    result[uniName] = QString();
                } else if (param.type == QLatin1String("bool")) {
                    result[uniName] = false;
                } else {
                    result[uniName] = 0.0;
                }
            } else {
                result[uniName] = param.defaultValue;
            }
        }

        // For image parameters: resolve relative paths against shader directory, emit wrap mode
        if (param.type == QLatin1String("image") && !uniName.isEmpty()) {
            const QString imgPath = result.value(uniName).toString();
            if (!imgPath.isEmpty() && QFileInfo(imgPath).isRelative()) {
                const QDir shaderDir = QFileInfo(info.sourcePath).absoluteDir();
                const QString resolved = shaderDir.absoluteFilePath(imgPath);
                if (QFile::exists(resolved)) {
                    result[uniName] = resolved;
                }
            }
            if (!result.value(uniName).isValid() || result.value(uniName).isNull()) {
                result[uniName] = QString();
            }
            result[uniName + QStringLiteral("_wrap")] = param.wrap.isEmpty() ? QStringLiteral("clamp") : param.wrap;

            const QString svgSizeKey = param.id + QStringLiteral("_svgSize");
            if (storedParams.contains(svgSizeKey)) {
                result[uniName + QStringLiteral("_svgSize")] = storedParams.value(svgSizeKey);
            }
        }
    }

    return result;
}

// ===============================================================================
// VariantMap conversions
// ===============================================================================

QVariantMap BaseShaderRegistry::shaderInfoToVariantMap(const BaseShaderInfo& info) const
{
    QVariantMap map;
    map[QLatin1String("id")] = info.id.isEmpty() ? QStringLiteral("unknown") : info.id;
    map[QLatin1String("name")] = info.name.isEmpty() ? info.id : info.name;
    map[QLatin1String("description")] = info.description;
    map[QLatin1String("author")] = info.author;
    map[QLatin1String("version")] = info.version;
    map[QLatin1String("isUserShader")] = info.isUserShader;
    map[QLatin1String("category")] = info.category;
    map[QLatin1String("isValid")] = info.isValid();

    if (info.shaderUrl.isValid()) {
        map[QLatin1String("shaderUrl")] = info.shaderUrl.toString();
    } else {
        map[QLatin1String("shaderUrl")] = QString();
    }

    if (!info.previewPath.isEmpty()) {
        map[QLatin1String("previewPath")] = info.previewPath;
    } else {
        map[QLatin1String("previewPath")] = QString();
    }

    // Parameters list
    QVariantList params;
    for (const ParameterInfo& param : info.parameters) {
        params.append(parameterInfoToVariantMap(param));
    }
    map[QLatin1String("parameters")] = params;

    // Presets list
    QVariantList presetsList;
    for (auto it = info.presets.constBegin(); it != info.presets.constEnd(); ++it) {
        QVariantMap presetMap;
        presetMap[QLatin1String("name")] = it.key();
        presetMap[QLatin1String("params")] = it.value();
        presetsList.append(presetMap);
    }
    map[QLatin1String("presets")] = presetsList;

    return map;
}

QVariantMap BaseShaderRegistry::parameterInfoToVariantMap(const ParameterInfo& param) const
{
    QVariantMap map;
    map[QLatin1String("id")] = param.id;
    map[QLatin1String("name")] = param.name;
    map[QLatin1String("type")] = param.type;
    map[QLatin1String("slot")] = param.slot;
    map[QLatin1String("mapsTo")] = param.uniformName();
    map[QLatin1String("useZoneColor")] = param.useZoneColor;
    if (!param.wrap.isEmpty()) {
        map[QLatin1String("wrap")] = param.wrap;
    }

    if (!param.group.isEmpty()) {
        map[QLatin1String("group")] = param.group;
    }
    if (param.defaultValue.isValid()) {
        map[QLatin1String("default")] = param.defaultValue;
    }
    if (param.minValue.isValid()) {
        map[QLatin1String("min")] = param.minValue;
    }
    if (param.maxValue.isValid()) {
        map[QLatin1String("max")] = param.maxValue;
    }

    return map;
}

} // namespace PlasmaZones
