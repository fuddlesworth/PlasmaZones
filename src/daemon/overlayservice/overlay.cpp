// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "internal.h"
#include "../overlayservice.h"
#include "../../core/logging.h"
#include "../../core/layout.h"
#include "../../core/layoutmanager.h"
#include "../../core/zone.h"
#include "../../core/geometryutils.h"
#include "../../core/utils.h"
#include "../../core/screenmanager.h"
#include "../../core/shaderregistry.h"
#include <QQuickWindow>
#include <QScreen>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QMutexLocker>
#include <QPointer>
#include "../../core/layersurface.h"

namespace PlasmaZones {

void OverlayService::initializeOverlay(QScreen* cursorScreen)
{
    // Determine if we should show on all monitors (cursorScreen == nullptr means all)
    const bool showOnAllMonitors = (cursorScreen == nullptr);

    m_visible = true;
    m_currentOverlayScreen = showOnAllMonitors ? nullptr : cursorScreen;

    // Initialize shader timing (shared across all monitors for synchronized effects)
    // Only start timer if invalid - preserves iTime across show/hide for less predictable animations
    ensureShaderTimerStarted(m_shaderTimer, m_shaderTimerMutex, m_lastFrameTime, m_frameCount);
    m_zoneDataDirty = true; // Rebuild zone data on next frame

    // When single-monitor mode, destroy overlay on screens we're switching away from (#136).
    // Must destroy (not just hide) because on Vulkan, window->hide() destroys the
    // VkSwapchainKHR but Qt doesn't properly reinitialize it on re-show. Destroying
    // the window ensures a fresh window is created via createOverlayWindow() below.
    if (!showOnAllMonitors) {
        const QList<QScreen*> screens = m_overlayWindows.keys();
        for (auto* screen : screens) {
            if (screen != cursorScreen) {
                destroyOverlayWindow(screen);
            }
        }
    }

    for (auto* screen : Utils::allScreens()) {
        // Skip screens that aren't the cursor's screen when single-monitor mode is enabled
        if (!showOnAllMonitors && screen != cursorScreen) {
            continue;
        }
        // Skip monitors/desktops/activities where PlasmaZones is disabled
        if (isContextDisabled(m_settings, Utils::screenIdentifier(screen), m_currentVirtualDesktop,
                              m_currentActivity)) {
            continue;
        }
        // Skip autotile-managed screens (overlay is for manual zone selection)
        if (m_excludedScreens.contains(Utils::screenIdentifier(screen))) {
            continue;
        }

        if (!m_overlayWindows.contains(screen)) {
            createOverlayWindow(screen);
        }
        if (auto* window = m_overlayWindows.value(screen)) {
            assertWindowOnScreen(window, screen);
            qCDebug(lcOverlay) << "initializeOverlay: screen=" << screen->name() << "screenGeom=" << screen->geometry()
                               << "availGeom=" << ScreenManager::actualAvailableGeometry(screen) << "windowScreen="
                               << (window->screen() ? window->screen()->name() : QStringLiteral("null"));
            updateOverlayWindow(screen);
            window->show();
            // On Vulkan, window->hide() destroys the swapchain and the scene graph
            // becomes inactive. Property writes (e.g. shaderSource) while hidden queue
            // update() requests that are lost when the scene graph reinitializes on
            // show(). Force a content update after show so the scene graph renders.
            window->update();
        }
    }

    // Type-mismatch recreation is not needed here: hide() destroys all overlay
    // windows, so initializeOverlay() always creates fresh windows with the
    // correct type. The old check compared isShaderOverlay against useShaderForScreen()
    // and recreated on mismatch, but could trigger runaway loops when both evaluations
    // raced during window setup.

    if (anyScreenUsesShader()) {
        updateZonesForAllWindows(); // Push initial zone data
        startShaderAnimation();
    }

    Q_EMIT visibilityChanged(true);
}

void OverlayService::updateLayout(Layout* layout)
{
    setLayout(layout);
    if (m_visible) {
        updateGeometries();

        // Flash zones to indicate layout change if enabled
        if (m_settings && m_settings->flashZonesOnSwitch()) {
            for (auto* window : std::as_const(m_overlayWindows)) {
                if (window) {
                    QMetaObject::invokeMethod(window, "flash");
                }
            }
        }

        // Shader state management - MUST be outside flashZonesOnSwitch block
        // to ensure shader animations work regardless of flash setting
        if (anyScreenUsesShader()) {
            // Ensure shader timing + updates continue after layout switch
            ensureShaderTimerStarted(m_shaderTimer, m_shaderTimerMutex, m_lastFrameTime, m_frameCount);
            m_zoneDataDirty = true;
            updateZonesForAllWindows();
            if (!m_shaderUpdateTimer || !m_shaderUpdateTimer->isActive()) {
                startShaderAnimation();
            }
        } else {
            stopShaderAnimation();
        }
    }
}

void OverlayService::updateGeometries()
{
    for (auto* screen : m_overlayWindows.keys()) {
        updateOverlayWindow(screen);
    }
    // Bump zone data version once after all per-screen updates complete,
    // then broadcast to all windows so shaders see the new version atomically.
    ++m_zoneDataVersion;
    for (auto* w : std::as_const(m_overlayWindows)) {
        if (w) {
            writeQmlProperty(w, QStringLiteral("zoneDataVersion"), m_zoneDataVersion);
        }
    }
}

void OverlayService::highlightZone(const QString& zoneId)
{
    // Mark zone data dirty for shader overlay updates
    m_zoneDataDirty = true;

    // Update the highlightedZoneId property on all overlay windows
    for (auto* window : std::as_const(m_overlayWindows)) {
        if (window) {
            writeQmlProperty(window, QStringLiteral("highlightedZoneId"), zoneId);
            // Clear multi-zone highlighting when using single zone
            writeQmlProperty(window, QStringLiteral("highlightedZoneIds"), QVariantList());
        }
    }
}

void OverlayService::highlightZones(const QStringList& zoneIds)
{
    // Mark zone data dirty for shader overlay updates
    m_zoneDataDirty = true;

    // Update the highlightedZoneIds property on all overlay windows
    QVariantList zoneIdList;
    for (const QString& zoneId : zoneIds) {
        zoneIdList.append(zoneId);
    }

    for (auto* window : std::as_const(m_overlayWindows)) {
        if (window) {
            writeQmlProperty(window, QStringLiteral("highlightedZoneIds"), zoneIdList);
            // Clear single zone highlighting when using multi-zone
            writeQmlProperty(window, QStringLiteral("highlightedZoneId"), QString());
        }
    }
}

void OverlayService::clearHighlight()
{
    // Mark zone data dirty for shader overlay updates
    m_zoneDataDirty = true;

    // Clear the highlight on all overlay windows
    for (auto* window : std::as_const(m_overlayWindows)) {
        if (window) {
            writeQmlProperty(window, QStringLiteral("highlightedZoneId"), QString());
            writeQmlProperty(window, QStringLiteral("highlightedZoneIds"), QVariantList());
        }
    }
}

void OverlayService::updateMousePosition(int cursorX, int cursorY)
{
    if (!m_visible) {
        return;
    }

    // Update mouse position on all overlay windows for shader effects
    for (auto* window : std::as_const(m_overlayWindows)) {
        if (window) {
            // Convert global cursor position to window-local coordinates
            const QPoint localPos = window->mapFromGlobal(QPoint(cursorX, cursorY));
            window->setProperty("mousePosition", QPointF(localPos.x(), localPos.y()));
        }
    }
}

void OverlayService::createOverlayWindow(QScreen* screen)
{
    if (m_overlayWindows.contains(screen)) {
        return;
    }

    // Choose overlay type based on shader settings for THIS screen's layout
    bool usingShader = useShaderForScreen(screen);

    // Expose overlayService to QML context for error reporting
    m_engine->rootContext()->setContextProperty(QStringLiteral("overlayService"), this);

    // Try shader overlay first, fall back to standard overlay if it fails
    QQuickWindow* window = nullptr;
    if (usingShader) {
        // Set labelsTexture before QML loads so ZoneShaderItem binding never sees undefined
        QImage placeholder(1, 1, QImage::Format_ARGB32);
        placeholder.fill(Qt::transparent);
        QVariantMap initProps;
        initProps.insert(QStringLiteral("labelsTexture"), QVariant::fromValue(placeholder));
        window =
            createQmlWindow(QUrl(QStringLiteral("qrc:/ui/RenderNodeOverlay.qml")), screen, "shader overlay", initProps);
        if (window) {
            qCInfo(lcOverlay) << "Overlay window created: RenderNodeOverlay (ZoneShaderItem) for screen"
                              << screen->name();
        } else {
            qCWarning(lcOverlay) << "Falling back to standard overlay";
            usingShader = false;
        }
    }
    if (!window) {
        window = createQmlWindow(QUrl(QStringLiteral("qrc:/ui/ZoneOverlay.qml")), screen, "overlay");
        if (!window) {
            return;
        }
    }

    // Set window size to cover full screen. Position is controlled by layer-surface
    // anchors (AnchorAll), not by setX/setY which are no-ops on layer surfaces.
    const QRect geom = screen->geometry();
    window->setWidth(geom.width());
    window->setHeight(geom.height());

    // Mark window type for reliable type detection
    window->setProperty("isShaderOverlay", usingShader);

    // Set shader-specific properties (use QQmlProperty so QML bindings see updates)
    // Use per-screen layout (same resolution as updateOverlayWindow) so each monitor
    // gets the correct shader when per-screen assignments differ
    Layout* screenLayout = resolveScreenLayout(screen);

    if (usingShader && screenLayout) {
        auto* registry = ShaderRegistry::instance();
        if (registry) {
            const QString shaderId = screenLayout->shaderId();
            const ShaderRegistry::ShaderInfo info = registry->shader(shaderId);
            qCDebug(lcOverlay) << "Overlay shader=" << shaderId << "multipass=" << info.isMultipass
                               << "bufferPaths=" << info.bufferShaderPaths.size();
            QVariantMap translatedParams = registry->translateParamsToUniforms(shaderId, screenLayout->shaderParams());
            applyShaderInfoToWindow(window, info, translatedParams);
        }
    }

    // Configure layer surface for full-screen overlay
    if (!configureLayerSurface(
            window, screen, LayerSurface::LayerOverlay, LayerSurface::KeyboardInteractivityNone,
            QStringLiteral("plasmazones-overlay-%1-%2").arg(Utils::screenIdentifier(screen)).arg(++m_scopeGeneration),
            LayerSurface::AnchorAll)) {
        qCWarning(lcOverlay) << "Failed to configure layer surface for overlay on" << screen->name();
        window->deleteLater();
        return;
    }

    window->setVisible(false);

    // Connect to screen geometry changes
    QPointer<QScreen> screenPtr = screen;
    connect(screen, &QScreen::geometryChanged, window, [this, screenPtr](const QRect& newGeom) {
        if (!screenPtr) {
            return;
        }
        if (auto* w = m_overlayWindows.value(screenPtr)) {
            // Only set size — position is controlled by layer-surface anchors (AnchorAll),
            // setX/setY are no-ops on layer surfaces.
            w->setWidth(newGeom.width());
            w->setHeight(newGeom.height());
            updateOverlayWindow(screenPtr);
        }
    });

    if (usingShader) {
        writeQmlProperty(window, QStringLiteral("zoneDataVersion"), m_zoneDataVersion);
    }

    m_overlayWindows.insert(screen, window);
}

void OverlayService::recreateOverlayWindowsOnTypeMismatch()
{
    QList<QScreen*> screensToRecreate;
    for (auto it = m_overlayWindows.constBegin(); it != m_overlayWindows.constEnd(); ++it) {
        auto* window = it.value();
        if (!window)
            continue;
        const bool windowIsShader = window->property("isShaderOverlay").toBool();
        const bool shouldUseShader = useShaderForScreen(it.key());
        if (windowIsShader != shouldUseShader)
            screensToRecreate.append(it.key());
    }
    if (screensToRecreate.isEmpty())
        return;

    const bool wasVisible = m_visible;
    if (wasVisible)
        stopShaderAnimation();

    for (QScreen* screen : screensToRecreate)
        destroyOverlayWindow(screen);
    for (QScreen* screen : screensToRecreate) {
        if (!isContextDisabled(m_settings, Utils::screenIdentifier(screen), m_currentVirtualDesktop,
                               m_currentActivity)) {
            createOverlayWindow(screen);
            if (auto* w = m_overlayWindows.value(screen)) {
                updateOverlayWindow(screen);
                if (wasVisible)
                    w->show();
            }
        }
    }
    if (wasVisible && anyScreenUsesShader()) {
        updateZonesForAllWindows();
        startShaderAnimation();
    }
}

void OverlayService::destroyOverlayWindow(QScreen* screen)
{
    if (auto* window = m_overlayWindows.take(screen)) {
        // Disconnect so no signals (e.g. geometryChanged) are delivered to a window we're destroying
        disconnect(screen, nullptr, window, nullptr);
        window->close();
        // destroy() synchronously tears down the platform window (wl_surface) and
        // its Vulkan resources (VkSurfaceKHR, VkSwapchainKHR, scene graph render loop).
        // Without this, deleteLater() defers cleanup to the next event loop iteration,
        // leaving stale Vulkan resources alive when createOverlayWindow() creates a new
        // surface on the same screen — causing swapchain/render loop corruption after
        // 2-3 show/hide cycles.
        window->destroy();
        window->deleteLater();
    }
}

void OverlayService::updateOverlayWindow(QScreen* screen)
{
    auto* window = m_overlayWindows.value(screen);
    if (!window) {
        return;
    }

    // Get the layout for this screen to use layout-specific settings
    // Prefer per-screen assignment, fall back to global active layout
    Layout* screenLayout = resolveScreenLayout(screen);

    // Update settings-based properties on the window itself (QML root)
    if (m_settings) {
        window->setProperty("highlightColor", m_settings->highlightColor());
        window->setProperty("inactiveColor", m_settings->inactiveColor());
        window->setProperty("borderColor", m_settings->borderColor());
        window->setProperty("activeOpacity", m_settings->activeOpacity());
        window->setProperty("inactiveOpacity", m_settings->inactiveOpacity());
        window->setProperty("borderWidth", m_settings->borderWidth());
        window->setProperty("borderRadius", m_settings->borderRadius());
        window->setProperty("enableBlur", m_settings->enableBlur());
        // Global setting is a master switch; per-layout setting can only further restrict
        bool showNumbers = m_settings->showZoneNumbers() && (!screenLayout || screenLayout->showZoneNumbers());
        window->setProperty("showNumbers", showNumbers);
        writeFontProperties(window, m_settings);
    }

    // Update shader-specific properties if using shader overlay
    // Only update if this window is actually a shader overlay window (check isShaderOverlay property)
    const bool windowIsShader = window->property("isShaderOverlay").toBool();
    const bool screenUsesShader = useShaderForScreen(screen);
    if (windowIsShader && screenUsesShader && screenLayout) {
        auto* registry = ShaderRegistry::instance();
        if (registry) {
            const QString shaderId = screenLayout->shaderId();
            const ShaderRegistry::ShaderInfo info = registry->shader(shaderId);
            QVariantMap translatedParams = registry->translateParamsToUniforms(shaderId, screenLayout->shaderParams());
            applyShaderInfoToWindow(window, info, translatedParams);
        }
    } else if (windowIsShader && !screenUsesShader) {
        // Clear shader properties if window is shader type but shaders are now disabled
        writeQmlProperty(window, QStringLiteral("shaderSource"), QUrl());
        writeQmlProperty(window, QStringLiteral("bufferShaderPath"), QString());
        writeQmlProperty(window, QStringLiteral("bufferShaderPaths"), QVariant::fromValue(QStringList()));
        writeQmlProperty(window, QStringLiteral("bufferFeedback"), false);
        writeQmlProperty(window, QStringLiteral("bufferScale"), 1.0);
        writeQmlProperty(window, QStringLiteral("bufferWrap"), QStringLiteral("clamp"));
        writeQmlProperty(window, QStringLiteral("bufferWraps"), QStringList());
        writeQmlProperty(window, QStringLiteral("bufferFilter"), QStringLiteral("linear"));
        writeQmlProperty(window, QStringLiteral("bufferFilters"), QStringList());
        writeQmlProperty(window, QStringLiteral("useDepthBuffer"), false);
        writeQmlProperty(window, QStringLiteral("shaderParams"), QVariantMap());
    }

    // Update zones on the window (QML root has the zones property).
    // Patch isHighlighted from overlay's highlightedZoneId/highlightedZoneIds so
    // ZoneDataProvider and zone components see the correct state.
    QVariantList zones = buildZonesList(screen);
    QVariantList patched = patchZonesWithHighlight(zones, window);

    // Pass previewZones (all zones with relative geometries) only when LayoutPreview mode is active
    bool anyZoneUsesPreview = false;
    for (const QVariant& z : std::as_const(patched)) {
        if (z.toMap().value(JsonKeys::OverlayDisplayMode).toInt() == 1) {
            anyZoneUsesPreview = true;
            break;
        }
    }
    writeQmlProperty(window, QStringLiteral("previewZones"), anyZoneUsesPreview ? patched : QVariantList{});
    writeQmlProperty(window, QStringLiteral("zones"), patched);

    // Shader overlay: zoneCount, highlightedCount, zoneDataVersion, labelsTexture
    if (windowIsShader && screenUsesShader) {
        int highlightedCount = 0;
        for (const QVariant& z : patched) {
            if (z.toMap().value(QLatin1String("isHighlighted")).toBool()) {
                ++highlightedCount;
            }
        }
        writeQmlProperty(window, QStringLiteral("zoneCount"), patched.size());
        writeQmlProperty(window, QStringLiteral("highlightedCount"), highlightedCount);
        updateLabelsTextureForWindow(window, patched, screen, screenLayout);
        // Note: zoneDataVersion is bumped and broadcast to all windows in
        // updateGeometries() after all per-screen updates complete. Do not
        // write it here — updateOverlayWindow() is called per-screen, and
        // writing the version mid-loop would cause inconsistent state across
        // windows (some see the new version, others the old one).
    }
}

} // namespace PlasmaZones
