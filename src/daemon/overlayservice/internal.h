// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>

#include <QFont>
#include <QGuiApplication>
#include <QMargins>
#include <QPalette>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>

#include <PhosphorLayer/Role.h>

#include "overlay_helpers.h"
#include "../../core/screenmanagerservice.h"
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

// Resolve target screen from a screen name/ID string with fallback to primary.
// Delegates to ScreenManager::resolvePhysicalScreen which handles virtual screen
// IDs, connector names, and falls back to primary screen.
inline QScreen* resolveTargetScreen(const QString& screenId)
{
    return resolvePhysicalScreen(screenId);
}

/// Resolve the PhosphorLayer anchors + margins for a surface that targets
/// @p vsGeom on a physical screen whose current geometry is @p physGeom.
///
/// Physical screens (vsGeom invalid or equal to physGeom) get AnchorAll +
/// zero margins so wlr-layer-shell sizes them to the full output. Virtual
/// screens (vsGeom is a strict sub-rect of physGeom) get Top|Left anchors
/// plus margins pinning them to the VS's top-left corner within the
/// physical screen's origin. This is the vocabulary wlr-layer-shell
/// understands — it has no "position window within output" verb.
///
/// Extracted because four independent call sites (overlay create, overlay
/// geometryChanged, overlay rekey, snap-assist create) previously inlined
/// the same math. Any drift between them showed up as virtual-screen
/// overlays landing on the wrong spot after a hot-plug.
struct VsLayerPlacement
{
    PhosphorLayer::Anchors anchors;
    QMargins margins;
};

inline VsLayerPlacement layerPlacementForVs(const QRect& vsGeom, const QRect& physGeom)
{
    const bool isVirtual = vsGeom.isValid() && vsGeom != physGeom;
    if (!isVirtual) {
        return {PhosphorLayer::AnchorAll, QMargins()};
    }
    const QRect clamped = vsGeom.intersected(physGeom);
    return {PhosphorLayer::Anchors{PhosphorLayer::Anchor::Top, PhosphorLayer::Anchor::Left},
            QMargins(clamped.x() - physGeom.x(), clamped.y() - physGeom.y(), 0, 0)};
}

/// Resolve anchors + margins for a floating surface whose absolute top-left
/// should land at @p topLeft within the physical screen @p physGeom. Always
/// Top|Left-anchored with margins relative to the physical origin.
/// Used by shader-preview paths that position the preview window at a
/// caller-chosen absolute coordinate inside the monitor (rather than at a
/// virtual-screen sub-region). Separate from layerPlacementForVs because the
/// VS variant treats "topLeft == physGeom.topLeft" as "fullscreen → AnchorAll",
/// which would drop the margins a floating preview needs.
inline VsLayerPlacement layerPlacementAt(const QPoint& topLeft, const QRect& physGeom)
{
    return {PhosphorLayer::Anchors{PhosphorLayer::Anchor::Top, PhosphorLayer::Anchor::Left},
            QMargins(qMax(0, topLeft.x() - physGeom.x()), qMax(0, topLeft.y() - physGeom.y()), 0, 0)};
}

/// Resolve target screen geometry for a screen ID (virtual or physical).
/// For virtual screens (format "physicalId/vs:N"), returns the virtual screen
/// geometry from ScreenManager. For physical screens, falls back to QScreen::geometry().
/// Returns the geometry the overlay window should cover.
inline QRect resolveScreenGeometry(const QString& screenId)
{
    auto* mgr = screenManager();
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
//
// @p vsGeom / @p physGeom identify the target screen. When they differ (i.e.
// the overlay covers a virtual screen that is a sub-rect of the physical
// screen), the wallpaper is cropped to the VS's portion so each VS samples
// its own slice of the physical monitor's wallpaper instead of each getting
// an aspect-centered full wallpaper. Pass empty rects for full-screen overlays.
inline void applyShaderInfoToWindow(QObject* window, const ShaderRegistry::ShaderInfo& info, const QVariantMap& params,
                                    const QRect& vsGeom = QRect(), const QRect& physGeom = QRect())
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
        const QImage wp = ShaderRegistry::loadWallpaperImage(vsGeom, physGeom);
        if (!wp.isNull()) {
            writeQmlProperty(window, QStringLiteral("wallpaperTexture"), QVariant::fromValue(wp));
        }
    }
    // shaderSource LAST — triggers statusChanged() → QML binding cascade
    writeQmlProperty(window, QStringLiteral("shaderSource"), info.shaderUrl);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Lock-mode check helper shared across overlayservice TUs
// ═══════════════════════════════════════════════════════════════════════════════

/// Check whether a snapping mode is locked for the given screen/desktop/activity context.
/// Used by zone selector, layout picker, and overlay update paths that must respect per-context lock state.
///
/// @param currentMode  The mode to check: 0 = manual, 1 = autotile, -1 = check both modes (default).
///
/// When currentMode is -1 (default), BOTH modes are checked. This is intentional (PR #247): a lock on
/// either mode blocks the zone selector for consistency with the OverlayService lock checks.
/// Previously, ZoneSelectorController only checked the current mode, causing inconsistencies when the
/// overlay reported a lock but the selector did not.
///
/// Pass the current mode explicitly when only the active mode's lock is relevant.
// contextLockKey is defined in Utils:: (src/core/utils.h)
inline bool isAnyModeLocked(ISettings* settings, const QString& screenId, int desktop, const QString& activity,
                            int currentMode = -1)
{
    if (!settings) {
        return false;
    }
    if (currentMode == 0 || currentMode == 1) {
        return settings->isContextLocked(Utils::contextLockKey(currentMode, screenId), desktop, activity);
    }
    // Default: check both modes (maintains PR #247 behavior)
    return settings->isContextLocked(Utils::contextLockKey(0, screenId), desktop, activity)
        || settings->isContextLocked(Utils::contextLockKey(1, screenId), desktop, activity);
}

// ═══════════════════════════════════════════════════════════════════════════════
// PhosphorZones::Zone selector helpers shared across overlayservice_selector*.cpp TUs
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
// Window destroy helpers (per-screen state struct fields)
// ═══════════════════════════════════════════════════════════════════════════════
// Individual destroy functions are implemented inline in each TU
// (destroyOverlayWindow, destroyZoneSelectorWindow, etc.) operating on
// m_screenStates[screenId] fields directly.

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
