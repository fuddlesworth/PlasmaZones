// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QVariant>
#include <QString>
#include <QQmlProperty>
#include <QElapsedTimer>
#include <QMutex>
#include <QMutexLocker>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonParseError>
#include <atomic>

#include "../../core/layersurface.h"
#include "../../core/logging.h"
#include "../../core/settings_interfaces.h"
#include "../../core/shaderregistry.h"
#include "../../core/utils.h"
#include "../config/configdefaults.h"

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// Shared helpers used by multiple overlayservice TUs
// (extracted from anonymous namespace in overlayservice.cpp)
// ═══════════════════════════════════════════════════════════════════════════════

inline void writeQmlProperty(QObject* object, const QString& name, const QVariant& value)
{
    if (!object) {
        return;
    }

    QQmlProperty prop(object, name);
    if (prop.isValid()) {
        prop.write(value);
    } else {
        object->setProperty(name.toUtf8().constData(), value);
    }
}

inline void writeFontProperties(QObject* window, const IZoneVisualizationSettings* settings)
{
    if (!window || !settings) {
        return;
    }
    writeQmlProperty(window, QStringLiteral("labelFontColor"), settings->labelFontColor());
    writeQmlProperty(window, QStringLiteral("fontFamily"), settings->labelFontFamily());
    writeQmlProperty(window, QStringLiteral("fontSizeScale"), settings->labelFontSizeScale());
    writeQmlProperty(window, QStringLiteral("fontWeight"), settings->labelFontWeight());
    writeQmlProperty(window, QStringLiteral("fontItalic"), settings->labelFontItalic());
    writeQmlProperty(window, QStringLiteral("fontUnderline"), settings->labelFontUnderline());
    writeQmlProperty(window, QStringLiteral("fontStrikeout"), settings->labelFontStrikeout());
}

// Fallback config when ISettings* is null (e.g. during teardown).
// Uses ConfigDefaults to stay in sync with the .kcfg single source of truth.
inline ZoneSelectorConfig defaultZoneSelectorConfig()
{
    return {ConfigDefaults::position(),          ConfigDefaults::layoutMode(),   ConfigDefaults::sizeMode(),
            ConfigDefaults::maxRows(),           ConfigDefaults::previewWidth(), ConfigDefaults::previewHeight(),
            ConfigDefaults::previewLockAspect(), ConfigDefaults::gridColumns(),  ConfigDefaults::triggerDistance()};
}

// ═══════════════════════════════════════════════════════════════════════════════
// DRY helpers replacing duplicated patterns across TUs
// ═══════════════════════════════════════════════════════════════════════════════

// Configure layer surface properties in one call.
// Replaces 7 occurrences of get + setScope + setLayer +
// setKeyboardInteractivity + setAnchors + setExclusiveZone pattern.
// Pass anchors = 0 to skip setAnchors (caller will set them separately).
inline void configureLayerSurface(QQuickWindow* window, QScreen* screen, LayerSurface::Layer layer,
                                  LayerSurface::KeyboardInteractivity keyboardInteractivity, const QString& scope,
                                  LayerSurface::Anchors anchors = LayerSurface::Anchors())
{
    if (!window) {
        return;
    }
    auto* layerSurface = LayerSurface::get(window);
    if (!layerSurface) {
        return;
    }
    layerSurface->setScreen(screen);
    layerSurface->setLayer(layer);
    layerSurface->setKeyboardInteractivity(keyboardInteractivity);
    if (anchors != LayerSurface::Anchors()) {
        layerSurface->setAnchors(anchors);
    }
    layerSurface->setExclusiveZone(-1);
    layerSurface->setScope(scope);
}

// Resolve target screen from a screen name/ID string with fallback to primary.
// Replaces 4 occurrences of "find screen by name with fallback to primary screen".
inline QScreen* resolveTargetScreen(const QString& screenId)
{
    QScreen* screen = nullptr;
    if (!screenId.isEmpty()) {
        screen = Utils::findScreenByIdOrName(screenId);
    }
    if (!screen) {
        screen = Utils::primaryScreen();
    }
    return screen;
}

// Write shader properties (shaderSource, bufferShaderPath, bufferShaderPaths,
// bufferFeedback, bufferScale, bufferWrap, shaderParams) from ShaderInfo to a QML window.
// Replaces 3 occurrences of the shader-info-to-window property push pattern.
inline void applyShaderInfoToWindow(QObject* window, const ShaderRegistry::ShaderInfo& info, const QVariantMap& params)
{
    if (!window) {
        return;
    }
    // Clear shaderSource FIRST to stop the render node from drawing the old shader
    // with the new (incompatible) config. Without this, switching from a multipass
    // shader tears down buffer FBOs while the old shader still references them,
    // which can crash NVIDIA's EGL driver in beginFrame().
    writeQmlProperty(window, QStringLiteral("shaderSource"), QString());

    // Set all auxiliary props BEFORE shaderSource — see shader.cpp comment
    writeQmlProperty(window, QStringLiteral("bufferShaderPath"), info.bufferShaderPath);
    QVariantList pathList;
    for (const QString& p : info.bufferShaderPaths) {
        pathList.append(p);
    }
    writeQmlProperty(window, QStringLiteral("bufferShaderPaths"), pathList);
    writeQmlProperty(window, QStringLiteral("bufferFeedback"), info.bufferFeedback);
    writeQmlProperty(window, QStringLiteral("bufferScale"), info.bufferScale);
    writeQmlProperty(window, QStringLiteral("bufferWrap"), info.bufferWrap);
    writeQmlProperty(window, QStringLiteral("shaderParams"), QVariant::fromValue(params));
    // Desktop wallpaper subscription
    writeQmlProperty(window, QStringLiteral("useWallpaper"), info.useWallpaper);
    if (info.useWallpaper) {
        const QImage wp = ShaderRegistry::loadWallpaperImage();
        if (!wp.isNull()) {
            writeQmlProperty(window, QStringLiteral("wallpaperTexture"), QVariant::fromValue(wp));
        }
    }
    // shaderSource LAST — triggers statusChanged() → QML binding cascade
    writeQmlProperty(window, QStringLiteral("shaderSource"), info.shaderUrl);
}

// Initialize shader timer if not already running. Prevents large iTimeDelta jumps
// by only starting if invalid. Replaces 3 occurrences of mutex-guarded timer init.
inline void ensureShaderTimerStarted(QElapsedTimer& timer, QMutex& mutex, std::atomic<qint64>& lastFrame,
                                     std::atomic<int>& frameCount)
{
    QMutexLocker locker(&mutex);
    if (!timer.isValid()) {
        timer.start();
        lastFrame.store(0);
        frameCount.store(0);
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Zone selector helpers shared across overlayservice_selector*.cpp TUs
// ═══════════════════════════════════════════════════════════════════════════════

// Convert ZoneSelectorPosition to layer surface anchors
inline LayerSurface::Anchors getAnchorsForPosition(ZoneSelectorPosition pos)
{
    switch (pos) {
    case ZoneSelectorPosition::TopLeft:
        return LayerSurface::Anchors(LayerSurface::AnchorTop | LayerSurface::AnchorLeft);
    case ZoneSelectorPosition::Top:
        return LayerSurface::Anchors(LayerSurface::AnchorTop | LayerSurface::AnchorLeft | LayerSurface::AnchorRight);
    case ZoneSelectorPosition::TopRight:
        return LayerSurface::Anchors(LayerSurface::AnchorTop | LayerSurface::AnchorRight);
    case ZoneSelectorPosition::Left:
        return LayerSurface::Anchors(LayerSurface::AnchorLeft | LayerSurface::AnchorTop | LayerSurface::AnchorBottom);
    case ZoneSelectorPosition::Center:
        // Anchor to all edges so the window fills the screen; the QML "center" state
        // positions the container in the middle of the full-screen transparent window.
        return LayerSurface::Anchors(LayerSurface::AnchorTop | LayerSurface::AnchorBottom | LayerSurface::AnchorLeft
                                     | LayerSurface::AnchorRight);
    case ZoneSelectorPosition::Right:
        return LayerSurface::Anchors(LayerSurface::AnchorRight | LayerSurface::AnchorTop | LayerSurface::AnchorBottom);
    case ZoneSelectorPosition::BottomLeft:
        return LayerSurface::Anchors(LayerSurface::AnchorBottom | LayerSurface::AnchorLeft);
    case ZoneSelectorPosition::Bottom:
        return LayerSurface::Anchors(LayerSurface::AnchorBottom | LayerSurface::AnchorLeft | LayerSurface::AnchorRight);
    case ZoneSelectorPosition::BottomRight:
        return LayerSurface::Anchors(LayerSurface::AnchorBottom | LayerSurface::AnchorRight);
    default:
        // Default to top anchors
        return LayerSurface::Anchors(LayerSurface::AnchorTop | LayerSurface::AnchorLeft | LayerSurface::AnchorRight);
    }
}

// Recursive QML item search by objectName
inline QQuickItem* findQmlItemByName(QQuickItem* item, const QString& objectName)
{
    if (!item) {
        return nullptr;
    }

    if (item->objectName() == objectName) {
        return item;
    }

    const auto children = item->childItems();
    for (auto* child : children) {
        if (auto* found = findQmlItemByName(child, objectName)) {
            return found;
        }
    }

    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Overlay helpers shared across overlayservice_overlay*.cpp TUs
// ═══════════════════════════════════════════════════════════════════════════════

// The zone model doesn't know about overlay highlights (keyboard/hover),
// so we patch isHighlighted here before passing to shaders
inline QVariantList patchZonesWithHighlight(const QVariantList& zones, QQuickWindow* window)
{
    if (!window) {
        return zones;
    }
    const QString hid = window->property("highlightedZoneId").toString();
    const QVariantList hids = window->property("highlightedZoneIds").toList();

    QVariantList out;
    for (const QVariant& z : zones) {
        QVariantMap m = z.toMap();
        const QString id = m.value(QLatin1String("id")).toString();
        bool hi = (!id.isEmpty() && id == hid);
        if (!hi) {
            for (const QVariant& v : hids) {
                if (v.toString() == id) {
                    hi = true;
                    break;
                }
            }
        }
        m[QLatin1String("isHighlighted")] = hi;
        out.append(m);
    }
    return out;
}

// Parse zones from JSON array. Returns empty list on parse error or invalid format.
// Shared by overlayservice_shader.cpp and overlayservice_snapassist.cpp.
inline QVariantList parseZonesJson(const QString& json, const char* context)
{
    QVariantList zones;
    if (json.isEmpty()) {
        return zones;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qCWarning(lcOverlay) << context << "invalid zones JSON:" << parseError.errorString();
        return zones;
    }
    if (!doc.isArray()) {
        qCWarning(lcOverlay) << context << "zones JSON is not an array";
        return zones;
    }
    for (const QJsonValue& v : doc.array()) {
        if (v.isObject()) {
            QVariantMap m;
            const QJsonObject o = v.toObject();
            for (auto it = o.begin(); it != o.end(); ++it) {
                m.insert(it.key(), it.value().toVariant());
            }
            zones.append(m);
        }
    }
    return zones;
}

} // namespace PlasmaZones
