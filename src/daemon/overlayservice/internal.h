// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>

#include <QFont>
#include <QGuiApplication>
#include <QPalette>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>

#include "overlay_helpers.h"
#include "../../core/screenmanager.h"
#include "../../core/settings_interfaces.h"
#include "../../core/interfaces.h"
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

/// Extracted label font/color settings from IZoneVisualizationSettings with fallback defaults.
/// Used by both updateLabelsTextureForWindow (overlay_data.cpp) and
/// buildLabelsImageForPreviewZones (shader.cpp) to avoid duplicating the 8+ settings reads.
struct LabelFontSettings
{
    QColor fontColor = Qt::white;
    QColor backgroundColor = Qt::black;
    QString fontFamily;
    qreal fontSizeScale = 1.0;
    int fontWeight = QFont::Bold;
    bool fontItalic = false;
    bool fontUnderline = false;
    bool fontStrikeout = false;
};

inline LabelFontSettings extractLabelFontSettings(const IZoneVisualizationSettings* settings)
{
    LabelFontSettings s;
    if (!settings) {
        return s;
    }
    s.fontColor = settings->labelFontColor();
    s.backgroundColor = QGuiApplication::palette().color(QPalette::Active, QPalette::Base);
    s.fontFamily = settings->labelFontFamily();
    s.fontSizeScale = settings->labelFontSizeScale();
    s.fontWeight = settings->labelFontWeight();
    s.fontItalic = settings->labelFontItalic();
    s.fontUnderline = settings->labelFontUnderline();
    s.fontStrikeout = settings->labelFontStrikeout();
    return s;
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
    // Clear shaderSource FIRST to stop the render node from drawing the old shader
    // with the new (incompatible) config. Without this, switching from a multipass
    // shader tears down buffer FBOs while the old shader still references them,
    // which can crash NVIDIA's EGL driver in beginFrame().
    writeQmlProperty(window, QStringLiteral("shaderSource"), QUrl());

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

/// Configure LayerSurface anchors and margins for a window on a virtual or physical screen.
/// For virtual screens (screenGeom != physScreen->geometry()), anchors top-left with offset margins.
/// For physical screens, anchors all four edges for full coverage.
inline void applyLayerShellScreenPosition(QWindow* window, QScreen* physScreen, const QRect& screenGeom)
{
    auto* layerSurface = LayerSurface::find(window);
    if (!layerSurface || !physScreen)
        return;

    layerSurface->setScreen(physScreen);

    const bool isVirtualScreen = screenGeom.isValid() && (screenGeom != physScreen->geometry());
    LayerSurface::BatchGuard batch(layerSurface);
    if (isVirtualScreen) {
        layerSurface->setAnchors(LayerSurface::Anchors(LayerSurface::AnchorTop | LayerSurface::AnchorLeft));
        const QRect physGeom = physScreen->geometry();
        // Clamp virtual screen geometry to physical screen bounds to prevent overlay escape
        const QRect clampedGeom = screenGeom.intersected(physGeom);
        layerSurface->setMargins(QMargins(clampedGeom.x() - physGeom.x(), clampedGeom.y() - physGeom.y(), 0, 0));
        // Explicit resize required — with only two anchors, LayerSurface won't auto-size
        window->resize(clampedGeom.size());
    } else {
        layerSurface->setAnchors(LayerSurface::AnchorAll);
    }
}

/// Resolve screen geometry from ScreenManager and apply layer-shell positioning.
/// Combines resolveScreenGeometry() + resolveTargetScreen() + applyLayerShellScreenPosition()
/// into a single call.  Returns the resolved geometry (invalid QRect on failure).
inline QRect updateWindowScreenPosition(QWindow* window, const QString& screenId)
{
    const QRect geom = resolveScreenGeometry(screenId);
    QScreen* physScreen = resolveTargetScreen(screenId);
    if (!physScreen || !geom.isValid()) {
        return QRect();
    }
    applyLayerShellScreenPosition(window, physScreen, geom);
    return geom;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Lock-mode check helper shared across overlayservice TUs
// ═══════════════════════════════════════════════════════════════════════════════

/// Check whether either snapping mode (0 = manual, 1 = autotile) is locked for
/// the given screen/desktop/activity context. Used by zone selector, layout
/// picker, and overlay update paths that must respect per-context lock state.
///
/// NOTE (behavioral change, PR #247): This intentionally checks BOTH modes
/// (0 = manual, 1 = autotile) regardless of which mode is currently active.
// contextLockKey is defined in Utils:: (src/core/utils.h)

/// A lock on either mode blocks the zone selector for consistency with the
/// OverlayService lock checks. Previously, ZoneSelectorController only
/// checked the current mode, which caused inconsistencies when the overlay
/// reported a lock but the selector did not.
inline bool isAnyModeLocked(ISettings* settings, const QString& screenId, int desktop, const QString& activity)
{
    if (!settings) {
        return false;
    }
    return settings->isContextLocked(Utils::contextLockKey(0, screenId), desktop, activity)
        || settings->isContextLocked(Utils::contextLockKey(1, screenId), desktop, activity);
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

// ═══════════════════════════════════════════════════════════════════════════════
// Common window destroy pattern shared across overlay, selector, OSD, shader TUs
// ═══════════════════════════════════════════════════════════════════════════════

/// Destroy a managed window: take from windowMap, disconnect physScreen signals,
/// close, deleteLater, remove from physScreenMap. Safe to call when screenId is
/// not present in the maps (no-op).
inline void destroyManagedWindow(QHash<QString, QQuickWindow*>& windowMap, QHash<QString, QScreen*>& physScreenMap,
                                 const QString& screenId)
{
    if (auto* window = windowMap.take(screenId)) {
        if (auto* physScreen = physScreenMap.value(screenId)) {
            QObject::disconnect(physScreen, nullptr, window, nullptr);
        }
        window->close();
        window->destroy();
        window->deleteLater();
    }
    physScreenMap.remove(screenId);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Shared color/opacity settings push for snap assist and layout picker
// ═══════════════════════════════════════════════════════════════════════════════

/// Write common zone color/opacity appearance settings to a QML window.
/// Used by snap assist and layout picker to avoid duplicating the 5-7 property writes.
inline void writeColorSettings(QObject* window, const IZoneVisualizationSettings* settings)
{
    if (!window || !settings) {
        return;
    }
    writeQmlProperty(window, QStringLiteral("highlightColor"), settings->highlightColor());
    writeQmlProperty(window, QStringLiteral("inactiveColor"), settings->inactiveColor());
    writeQmlProperty(window, QStringLiteral("borderColor"), settings->borderColor());
    writeQmlProperty(window, QStringLiteral("activeOpacity"), settings->activeOpacity());
    writeQmlProperty(window, QStringLiteral("inactiveOpacity"), settings->inactiveOpacity());
}

// patchZonesWithHighlight, parseZonesJson, ensureShaderTimerStarted, getAnchorsForPosition
// are defined in overlay_helpers.h (included above)

} // namespace PlasmaZones
