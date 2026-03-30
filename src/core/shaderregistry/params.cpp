// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Shader parameter validation, coercion, and uniform translation.
// Part of ShaderRegistry — split from shaderregistry.cpp for SRP.

#include "../shaderregistry.h"
#include "../logging.h"
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <QMutexLocker>

namespace PlasmaZones {

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

    // Multipass shader metadata (for editor preview)
    map[QStringLiteral("bufferShaderPaths")] = info.bufferShaderPaths;
    map[QStringLiteral("bufferFeedback")] = info.bufferFeedback;
    map[QStringLiteral("bufferScale")] = info.bufferScale;
    map[QStringLiteral("bufferWrap")] = info.bufferWrap;
    if (!info.bufferWraps.isEmpty()) {
        map[QStringLiteral("bufferWraps")] = QVariant::fromValue(info.bufferWraps);
    }
    map[QStringLiteral("wallpaper")] = info.useWallpaper;
    map[QStringLiteral("depthBuffer")] = info.useDepthBuffer;

    // Parameters list (empty list is OK for D-Bus)
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
// Plasma Wallpaper Path Resolution
// ═══════════════════════════════════════════════════════════════════════════════

std::unique_ptr<IWallpaperProvider> ShaderRegistry::s_wallpaperProvider;
QString ShaderRegistry::s_cachedWallpaperPath;
QImage ShaderRegistry::s_cachedWallpaperImage;
qint64 ShaderRegistry::s_cachedWallpaperMtime = 0;
QMutex ShaderRegistry::s_wallpaperCacheMutex;

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
        path = wallpaperPath(); // acquires lock internally
        lock.relock();
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
    qCDebug(lcCore) << "Loaded and cached wallpaper image:" << path << s_cachedWallpaperImage.size();
    return s_cachedWallpaperImage;
}

void ShaderRegistry::invalidateWallpaperCache()
{
    QMutexLocker lock(&s_wallpaperCacheMutex);
    s_cachedWallpaperPath.clear();
    s_cachedWallpaperImage = QImage();
    s_cachedWallpaperMtime = 0;
    s_wallpaperProvider.reset(); // force re-detection on next call
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

            // Handle color type - keep as QString for D-Bus compatibility (QColor is not
            // registered with D-Bus and causes marshalling crash when returned over D-Bus)
            if (param.type == QLatin1String("color") && value.typeId() == QMetaType::QString) {
                QColor color(value.toString());
                if (color.isValid()) {
                    result[uniformName] = color.name(QColor::HexArgb);
                } else {
                    // Fallback to default or transparent black (never null QVariant for D-Bus safety)
                    QColor defColor(param.defaultValue.toString());
                    result[uniformName] =
                        defColor.isValid() ? defColor.name(QColor::HexArgb) : QStringLiteral("#00000000");
                }
            } else {
                result[uniformName] = value;
            }
        } else {
            // Use default value for missing parameters
            // Guard against null QVariant which crashes D-Bus marshalling
            if (param.type == QLatin1String("color")) {
                QColor color(param.defaultValue.toString());
                result[uniformName] = color.isValid() ? color.name(QColor::HexArgb) : QStringLiteral("#00000000");
            } else if (!param.defaultValue.isValid() || param.defaultValue.isNull()) {
                // Provide type-appropriate empty default to avoid null QVariant in D-Bus
                if (param.type == QLatin1String("image")) {
                    result[uniformName] = QString();
                } else if (param.type == QLatin1String("bool")) {
                    result[uniformName] = false;
                } else {
                    result[uniformName] = 0.0;
                }
            } else {
                result[uniformName] = param.defaultValue;
            }
        }

        // For image parameters: resolve relative paths against shader directory, emit wrap mode
        if (param.type == QLatin1String("image") && !uniformName.isEmpty()) {
            const QString imgPath = result.value(uniformName).toString();
            if (!imgPath.isEmpty() && QFileInfo(imgPath).isRelative()) {
                const QDir shaderDir = QFileInfo(info.sourcePath).absoluteDir();
                const QString resolved = shaderDir.absoluteFilePath(imgPath);
                if (QFile::exists(resolved)) {
                    result[uniformName] = resolved;
                }
            }
            // Ensure image params are never null QVariant (causes D-Bus marshalling crash)
            if (!result.value(uniformName).isValid() || result.value(uniformName).isNull()) {
                result[uniformName] = QString();
            }
            result[uniformName + QStringLiteral("_wrap")] = param.wrap.isEmpty() ? QStringLiteral("clamp") : param.wrap;

            // Pass through SVG render size if present in stored params
            const QString svgSizeKey = param.id + QStringLiteral("_svgSize");
            if (storedParams.contains(svgSizeKey)) {
                result[uniformName + QStringLiteral("_svgSize")] = storedParams.value(svgSizeKey);
            }
        }
    }

    return result;
}

} // namespace PlasmaZones
