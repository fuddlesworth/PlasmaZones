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

#include <LayerShellQt/Window>

#include "../../core/logging.h"
#include "../../core/screenmanager.h"
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

// Configure LayerShellQt window properties in one call.
// Replaces 7 occurrences of get-LayerShellQt::Window + setScope + setLayer +
// setKeyboardInteractivity + setAnchors + setExclusiveZone pattern.
// Pass anchors = 0 to skip setAnchors (caller will set them separately).
inline void configureLayerShell(QQuickWindow* window, QScreen* screen, int layer, int keyboardInteractivity,
                                const QString& scope,
                                LayerShellQt::Window::Anchors anchors = LayerShellQt::Window::Anchors())
{
    if (!window) {
        return;
    }
    auto* layerWindow = LayerShellQt::Window::get(window);
    if (!layerWindow) {
        return;
    }
    layerWindow->setScreen(screen);
    layerWindow->setLayer(static_cast<LayerShellQt::Window::Layer>(layer));
    layerWindow->setKeyboardInteractivity(
        static_cast<LayerShellQt::Window::KeyboardInteractivity>(keyboardInteractivity));
    if (anchors != LayerShellQt::Window::Anchors()) {
        layerWindow->setAnchors(anchors);
    }
    layerWindow->setExclusiveZone(-1);
    layerWindow->setScope(scope);
}

// Resolve target screen from a screen name/ID string with fallback to primary.
// Delegates to ScreenManager::resolvePhysicalScreen which handles virtual screen
// IDs, connector names, and falls back to primary screen.
inline QScreen* resolveTargetScreen(const QString& screenId)
{
    return ScreenManager::resolvePhysicalScreen(screenId);
}

/// Resolve target screen geometry for a screen ID (virtual or physical).
/// For virtual screens (format "physicalId/vs:N"), returns the virtual screen
/// geometry from ScreenManager. For physical screens, falls back to QScreen::geometry().
/// Returns the geometry the overlay window should cover.
inline QRect resolveScreenGeometry(const QString& screenId)
{
    auto* mgr = ScreenManager::instance();
    if (mgr) {
        QRect geom = mgr->screenGeometry(screenId);
        if (geom.isValid()) {
            return geom;
        }
    }
    // Fallback: physical screen geometry only. This path is hit when ScreenManager
    // is unavailable (e.g. early startup) or when screenId doesn't match any virtual
    // screen. The caller gets raw QScreen::geometry() which is always the full
    // physical monitor — acceptable as a last-resort fallback.
    QScreen* screen = resolveTargetScreen(screenId);
    return screen ? screen->geometry() : QRect();
}

// Write shader properties (shaderSource, bufferShaderPath, bufferShaderPaths,
// bufferFeedback, bufferScale, bufferWrap, shaderParams) from ShaderInfo to a QML window.
// Replaces 3 occurrences of the shader-info-to-window property push pattern.
inline void applyShaderInfoToWindow(QObject* window, const ShaderRegistry::ShaderInfo& info, const QVariantMap& params)
{
    if (!window) {
        return;
    }
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

/// Configure LayerShellQt anchors and margins for a window on a virtual or physical screen.
/// For virtual screens (screenGeom != physScreen->geometry()), anchors top-left with offset margins.
/// For physical screens, anchors all four edges for full coverage.
inline void applyLayerShellScreenPosition(QWindow* window, QScreen* physScreen, const QRect& screenGeom)
{
    auto* layerWindow = LayerShellQt::Window::get(window);
    if (!layerWindow || !physScreen)
        return;

    layerWindow->setScreen(physScreen);

    const bool isVirtualScreen = screenGeom.isValid() && (screenGeom != physScreen->geometry());
    if (isVirtualScreen) {
        layerWindow->setAnchors(
            LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorLeft));
        const QRect physGeom = physScreen->geometry();
        layerWindow->setMargins(QMargins(screenGeom.x() - physGeom.x(), screenGeom.y() - physGeom.y(), 0, 0));
    } else {
        layerWindow->setAnchors(
            LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorBottom
                                          | LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight));
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Zone selector helpers shared across overlayservice_selector*.cpp TUs
// ═══════════════════════════════════════════════════════════════════════════════

// Convert ZoneSelectorPosition to LayerShellQt anchors
inline LayerShellQt::Window::Anchors getAnchorsForPosition(ZoneSelectorPosition pos)
{
    switch (pos) {
    case ZoneSelectorPosition::TopLeft:
        return LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorLeft);
    case ZoneSelectorPosition::Top:
        return LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorLeft
                                             | LayerShellQt::Window::AnchorRight);
    case ZoneSelectorPosition::TopRight:
        return LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorRight);
    case ZoneSelectorPosition::Left:
        return LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorTop
                                             | LayerShellQt::Window::AnchorBottom);
    case ZoneSelectorPosition::Center:
        // Anchor to all edges so the window fills the screen; the QML "center" state
        // positions the container in the middle of the full-screen transparent window.
        return LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorBottom
                                             | LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight);
    case ZoneSelectorPosition::Right:
        return LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorRight | LayerShellQt::Window::AnchorTop
                                             | LayerShellQt::Window::AnchorBottom);
    case ZoneSelectorPosition::BottomLeft:
        return LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorBottom | LayerShellQt::Window::AnchorLeft);
    case ZoneSelectorPosition::Bottom:
        return LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorBottom | LayerShellQt::Window::AnchorLeft
                                             | LayerShellQt::Window::AnchorRight);
    case ZoneSelectorPosition::BottomRight:
        return LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorBottom | LayerShellQt::Window::AnchorRight);
    default:
        // Default to top anchors
        return LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorLeft
                                             | LayerShellQt::Window::AnchorRight);
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
