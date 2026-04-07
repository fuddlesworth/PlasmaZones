// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>

#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>

#include "overlay_helpers.h"
#include "../../core/settings_interfaces.h"
#include "../../core/shaderregistry.h"
#include "../../core/utils.h"
#include "../config/configdefaults.h"

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// Shared helpers used by multiple overlayservice TUs
// Pure helpers (writeQmlProperty, patchZonesWithHighlight, parseZonesJson,
// ensureShaderTimerStarted, getAnchorsForPosition) are in overlay_helpers.h
// so tests can include them without pulling in ConfigDefaults/ShaderRegistry.
// ═══════════════════════════════════════════════════════════════════════════════

inline void resetOsdOverlayState(QObject* window)
{
    if (!window) {
        return;
    }
    // Clear both overlay states — callers set the one they need afterwards.
    // locked is explicitly set by every call site, but reset here for safety
    // when a caller forgets (e.g. showDisabledOsd doesn't set locked at all).
    writeQmlProperty(window, QStringLiteral("locked"), false);
    writeQmlProperty(window, QStringLiteral("disabled"), false);
    writeQmlProperty(window, QStringLiteral("disabledReason"), QString());
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

inline void writeAutotileMetadata(QObject* window, bool showMasterDot, bool producesOverlappingZones,
                                  const QString& zoneNumberDisplay = QStringLiteral("all"), int masterCount = 1)
{
    if (!window) {
        return;
    }
    writeQmlProperty(window, QStringLiteral("showMasterDot"), showMasterDot);
    writeQmlProperty(window, QStringLiteral("producesOverlappingZones"), producesOverlappingZones);
    writeQmlProperty(window, QStringLiteral("zoneNumberDisplay"), zoneNumberDisplay);
    writeQmlProperty(window, QStringLiteral("masterCount"), masterCount);
}

// Fallback config when ISettings* is null (e.g. during teardown).
// Uses ConfigDefaults to stay in sync with the .kcfg single source of truth.
inline ZoneSelectorConfig defaultZoneSelectorConfig()
{
    return {ConfigDefaults::position(),          ConfigDefaults::layoutMode(),   ConfigDefaults::sizeMode(),
            ConfigDefaults::maxRows(),           ConfigDefaults::previewWidth(), ConfigDefaults::previewHeight(),
            ConfigDefaults::previewLockAspect(), ConfigDefaults::gridColumns(),  ConfigDefaults::triggerDistance()};
}

// Configure layer surface properties in one call.
// Pass std::nullopt for anchors to skip setting them (caller will set separately).
// This avoids conflating AnchorNone (a valid value) with "not provided".
// exclusiveZone defaults to -1 (overlay ignores panels); use 0 for sensors.
// Returns true if configuration succeeded, false if layer-shell is unavailable
// or LayerSurface creation failed. Callers should check the return value and
// handle the unsupported case (e.g. skip showing the overlay, or degrade gracefully).
[[nodiscard]] inline bool configureLayerSurface(QQuickWindow* window, QScreen* screen, LayerSurface::Layer layer,
                                                LayerSurface::KeyboardInteractivity keyboardInteractivity,
                                                const QString& scope,
                                                std::optional<LayerSurface::Anchors> anchors = std::nullopt,
                                                int32_t exclusiveZone = -1)
{
    if (!window) {
        return false;
    }
    if (!LayerSurface::isSupported()) {
        qCWarning(lcOverlay) << "configureLayerSurface: zwlr_layer_shell_v1 not available —"
                             << "window will be created as xdg_toplevel (wrong stacking/anchoring)."
                             << "This is expected on compositors without layer-shell support (e.g. GNOME/Mutter).";
        return false;
    }
    auto* layerSurface = LayerSurface::get(window);
    if (!layerSurface) {
        qCWarning(lcOverlay) << "configureLayerSurface: LayerSurface::get() returned nullptr for window"
                             << window->objectName() << "— layer surface properties will not be applied";
        return false;
    }
    // Batch all property changes into a single propertiesChanged() emission
    // so the QPA plugin only does one applyProperties()+commit round-trip.
    LayerSurface::BatchGuard batch(layerSurface);
    layerSurface->setScreen(screen);
    layerSurface->setLayer(layer);
    layerSurface->setKeyboardInteractivity(keyboardInteractivity);
    if (anchors.has_value()) {
        layerSurface->setAnchors(*anchors);
    }
    layerSurface->setExclusiveZone(exclusiveZone);
    layerSurface->setScope(scope);
    return true;
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
    writeQmlProperty(window, QStringLiteral("bufferShaderPath"),
                     info.bufferShaderPaths.isEmpty() ? QString() : info.bufferShaderPaths.constFirst());
    writeQmlProperty(window, QStringLiteral("bufferShaderPaths"), QVariant::fromValue(info.bufferShaderPaths));
    writeQmlProperty(window, QStringLiteral("bufferFeedback"), info.bufferFeedback);
    writeQmlProperty(window, QStringLiteral("bufferScale"), info.bufferScale);
    writeQmlProperty(window, QStringLiteral("bufferWrap"), info.bufferWrap);
    writeQmlProperty(window, QStringLiteral("bufferWraps"), QVariant::fromValue(info.bufferWraps));
    writeQmlProperty(window, QStringLiteral("bufferFilter"), info.bufferFilter);
    writeQmlProperty(window, QStringLiteral("bufferFilters"), QVariant::fromValue(info.bufferFilters));
    writeQmlProperty(window, QStringLiteral("useDepthBuffer"), info.useDepthBuffer);
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

// ═══════════════════════════════════════════════════════════════════════════════
// Zone selector helpers shared across overlayservice_selector*.cpp TUs
// ═══════════════════════════════════════════════════════════════════════════════

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

} // namespace PlasmaZones
