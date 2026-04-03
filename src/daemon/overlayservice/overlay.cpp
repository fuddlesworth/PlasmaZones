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
#include "../../core/virtualscreen.h"
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

void OverlayService::initializeOverlay(QScreen* cursorScreen, const QPoint& cursorPos)
{
    // Determine if we should show on all monitors (cursorScreen == nullptr means all)
    const bool showOnAllMonitors = (cursorScreen == nullptr);

    m_visible = true;

    // Initialize shader timing (shared across all monitors for synchronized effects)
    // Only start timer if invalid - preserves iTime across show/hide for less predictable animations
    ensureShaderTimerStarted(m_shaderTimer, m_shaderTimerMutex, m_lastFrameTime, m_frameCount);
    m_zoneDataDirty = true; // Rebuild zone data on next frame

    // Determine the cursor's effective screen ID for single-monitor filtering.
    // For virtual screens we must resolve to the specific virtual screen under
    // the cursor, not just the physical monitor (all virtual screens on one
    // physical monitor share the same QScreen*).
    // Resolve to effective (virtual) screen ID under the cursor
    QString cursorEffectiveId;
    if (!showOnAllMonitors && cursorScreen) {
        QPoint pos = (cursorPos.x() >= 0) ? cursorPos : QCursor::pos();
        cursorEffectiveId = Utils::effectiveScreenIdAt(pos, cursorScreen);
    } else if (cursorScreen) {
        cursorEffectiveId = Utils::screenIdentifier(cursorScreen);
    }

    // Store the effective screen ID for cross-virtual-screen detection in showAtPosition()
    m_currentOverlayScreenId = showOnAllMonitors ? QString() : cursorEffectiveId;

    // When single-monitor mode, destroy overlay on screens we're switching away from (#136).
    // Must destroy (not just hide) because on Vulkan, window->hide() destroys the
    // VkSwapchainKHR but Qt doesn't properly reinitialize it on re-show. Destroying
    // the window ensures a fresh window is created via createOverlayWindow() below.
    if (!showOnAllMonitors) {
        const QStringList screenIds = m_overlayWindows.keys();
        for (const QString& screenId : screenIds) {
            if (screenId != cursorEffectiveId) {
                destroyOverlayWindow(screenId);
            }
        }
    }

    // Iterate effective screen IDs (includes virtual screens when configured)
    auto* mgr = ScreenManager::instance();
    const QStringList effectiveIds = mgr ? mgr->effectiveScreenIds() : QStringList();

    // Use effective screen IDs if ScreenManager is available, otherwise fall back to physical screens
    if (mgr && !effectiveIds.isEmpty()) {
        for (const QString& screenId : effectiveIds) {
            QScreen* physScreen = mgr->physicalQScreenFor(screenId);
            if (!physScreen) {
                continue;
            }
            // Skip screens that aren't the cursor's effective screen when single-monitor mode is enabled
            if (!showOnAllMonitors && screenId != cursorEffectiveId) {
                continue;
            }
            // Skip monitors/desktops/activities where PlasmaZones is disabled
            if (isContextDisabled(m_settings, screenId, m_currentVirtualDesktop, m_currentActivity)) {
                continue;
            }
            // Skip autotile-managed screens (overlay is for manual zone selection)
            if (m_excludedScreens.contains(screenId)) {
                continue;
            }

            const QRect geom = mgr->screenGeometry(screenId);
            if (!m_overlayWindows.contains(screenId)) {
                createOverlayWindow(screenId, physScreen, geom);
            }
            if (auto* window = m_overlayWindows.value(screenId)) {
                const QRect storedGeom = m_overlayGeometries.value(screenId, geom);
                assertWindowOnScreen(window, physScreen, storedGeom);
                qCDebug(lcOverlay) << "initializeOverlay: screenId=" << screenId << "geom=" << geom << "windowScreen="
                                   << (window->screen() ? window->screen()->name() : QStringLiteral("null"));
                updateOverlayWindow(screenId, physScreen);
                window->show();
            }
        }
    } else {
        // Fallback: no ScreenManager, use physical screens directly
        for (auto* screen : Utils::allScreens()) {
            if (!showOnAllMonitors && screen != cursorScreen) {
                continue;
            }
            if (isContextDisabled(m_settings, Utils::screenIdentifier(screen), m_currentVirtualDesktop,
                                  m_currentActivity)) {
                continue;
            }
            if (m_excludedScreens.contains(Utils::screenIdentifier(screen))) {
                continue;
            }

            const QString screenId = Utils::screenIdentifier(screen);
            if (!m_overlayWindows.contains(screenId)) {
                createOverlayWindow(screen);
            }
            if (auto* window = m_overlayWindows.value(screenId)) {
                assertWindowOnScreen(window, screen);
                qCDebug(lcOverlay) << "initializeOverlay (physical fallback): screenId=" << screenId
                                   << "physGeom=" << screen->geometry()
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
    for (const QString& screenId : m_overlayWindows.keys()) {
        QScreen* physScreen = m_overlayPhysScreens.value(screenId);
        if (physScreen) {
            updateOverlayWindow(screenId, physScreen);
        }
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

    // Update mouse position on all overlay windows for shader effects.
    // Use m_overlayGeometries for coordinate translation instead of
    // window->mapFromGlobal(), which may return wrong coordinates before the
    // first frame because QWindow::geometry() doesn't reflect LayerShell
    // positioning yet.
    for (auto it = m_overlayWindows.constBegin(); it != m_overlayWindows.constEnd(); ++it) {
        if (it.value()) {
            const QRect targetGeom = m_overlayGeometries.value(it.key());
            if (!targetGeom.isValid()) {
                // No overlay geometry recorded for this screen ID yet. Skip rather than
                // falling back to QWindow::mapFromGlobal(), which is unreliable on Wayland
                // with LayerShell until after the first frame (geometry not yet applied).
                qCWarning(lcOverlay) << "updateMousePosition: no overlay geometry for screen" << it.key()
                                     << "— skipping mouse position update";
                continue;
            }
            const QPointF local(cursorX - targetGeom.x(), cursorY - targetGeom.y());
            it.value()->setProperty("mousePosition", local);
        }
    }
}

void OverlayService::createOverlayWindow(QScreen* screen)
{
    const QString screenId = Utils::screenIdentifier(screen);
    auto* mgr = ScreenManager::instance();
    QRect geom = (mgr && mgr->screenGeometry(screenId).isValid()) ? mgr->screenGeometry(screenId) : screen->geometry();
    createOverlayWindow(screenId, screen, geom);
}

void OverlayService::createOverlayWindow(const QString& screenId, QScreen* physScreen, const QRect& geometry)
{
    if (m_overlayWindows.contains(screenId)) {
        return;
    }

    // Choose overlay type based on shader settings for THIS screen's layout
    bool usingShader = useShaderForScreen(screenId);

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
        window = createQmlWindow(QUrl(QStringLiteral("qrc:/ui/RenderNodeOverlay.qml")), physScreen, "shader overlay",
                                 initProps);
        if (window) {
            qCInfo(lcOverlay) << "Overlay window created: RenderNodeOverlay (ZoneShaderItem) for screen" << screenId;
        } else {
            qCWarning(lcOverlay) << "Falling back to standard overlay";
            usingShader = false;
        }
    }
    if (!window) {
        window = createQmlWindow(QUrl(QStringLiteral("qrc:/ui/ZoneOverlay.qml")), physScreen, "overlay");
        if (!window) {
            return;
        }
    }

    // Set window size to cover the target screen area (physical or virtual).
    // Position is controlled by layer-surface anchors + margins for virtual screens,
    // so setX/setY are only used as hints for mapFromGlobal.
    window->setWidth(geometry.width());
    window->setHeight(geometry.height());

    // Mark window type for reliable type detection
    window->setProperty("isShaderOverlay", usingShader);

    // Set shader-specific properties (use QQmlProperty so QML bindings see updates)
    // Use per-screen layout (same resolution as updateOverlayWindow) so each monitor
    // gets the correct shader when per-screen assignments differ
    Layout* screenLayout = resolveScreenLayout(screenId);

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

    // Configure layer surface for overlay.
    // For virtual screens, the window is parented to the physical QScreen but sized
    // to the virtual screen geometry. Anchors are NOT set to all-edges for virtual
    // screens since the window doesn't cover the full physical screen.
    if (!configureLayerSurface(window, physScreen, LayerSurface::LayerOverlay, LayerSurface::KeyboardInteractivityNone,
                               QStringLiteral("plasmazones-overlay-%1-%2").arg(screenId).arg(++m_scopeGeneration))) {
        qCWarning(lcOverlay) << "Failed to configure layer surface for overlay on" << screenId;
        window->deleteLater();
        return;
    }
    // Apply virtual screen positioning (anchors + margins) after configureLayerSurface
    applyLayerShellScreenPosition(window, physScreen, geometry);

    window->setVisible(false);

    // Connect to physical screen geometry changes (only for physical screens;
    // virtual screen geometry changes are handled via ScreenManager signals)
    const bool isVirtualScreen = VirtualScreenId::isVirtual(screenId);
    if (!isVirtualScreen) {
        QPointer<QScreen> screenPtr = physScreen;
        const QString sid = screenId; // Capture by value for lambda
        connect(physScreen, &QScreen::geometryChanged, window, [this, screenPtr, sid](const QRect& newGeom) {
            if (!screenPtr) {
                return;
            }
            if (auto* w = m_overlayWindows.value(sid)) {
                // Only set size — position is controlled by layer-surface anchors (AnchorAll),
                // setX/setY are no-ops on layer surfaces.
                w->setWidth(newGeom.width());
                w->setHeight(newGeom.height());
                m_overlayGeometries[sid] = newGeom;
                updateOverlayWindow(sid, screenPtr);
            }
        });
    }

    if (usingShader) {
        writeQmlProperty(window, QStringLiteral("zoneDataVersion"), m_zoneDataVersion);
    }

    m_overlayWindows.insert(screenId, window);
    m_overlayPhysScreens.insert(screenId, physScreen);
    m_overlayGeometries.insert(screenId, geometry);
}

void OverlayService::recreateOverlayWindowsOnTypeMismatch()
{
    QStringList screensToRecreate;
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

    // Snapshot phys screens before destroying
    QHash<QString, QScreen*> savedPhysScreens = m_overlayPhysScreens;
    QHash<QString, QRect> savedGeometries = m_overlayGeometries;

    for (const QString& screenId : screensToRecreate)
        destroyOverlayWindow(screenId);
    for (const QString& screenId : screensToRecreate) {
        if (!isContextDisabled(m_settings, screenId, m_currentVirtualDesktop, m_currentActivity)) {
            QScreen* physScreen = savedPhysScreens.value(screenId);
            if (!physScreen)
                continue;
            const QRect geom = savedGeometries.value(screenId, physScreen->geometry());
            createOverlayWindow(screenId, physScreen, geom);
            if (auto* w = m_overlayWindows.value(screenId)) {
                updateOverlayWindow(screenId, physScreen);
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
    const QString screenId = Utils::screenIdentifier(screen);
    destroyOverlayWindow(screenId);
}

void OverlayService::destroyOverlayWindow(const QString& screenId)
{
    destroyManagedWindow(m_overlayWindows, m_overlayPhysScreens, screenId);
    m_overlayGeometries.remove(screenId);
}

void OverlayService::updateOverlayWindow(QScreen* screen)
{
    const QString screenId = Utils::screenIdentifier(screen);
    updateOverlayWindow(screenId, screen);
}

void OverlayService::updateOverlayWindow(const QString& screenId, QScreen* physScreen)
{
    auto* window = m_overlayWindows.value(screenId);
    if (!window) {
        return;
    }

    // Get the layout for this screen to use layout-specific settings
    // Prefer per-screen assignment, fall back to global active layout
    Layout* screenLayout = resolveScreenLayout(screenId);

    // Update settings-based properties on the window itself (QML root)
    if (m_settings) {
        writeColorSettings(window, m_settings);
        writeQmlProperty(window, QStringLiteral("borderWidth"), m_settings->borderWidth());
        writeQmlProperty(window, QStringLiteral("borderRadius"), m_settings->borderRadius());
        writeQmlProperty(window, QStringLiteral("enableBlur"), m_settings->enableBlur());
        // Global setting is a master switch; per-layout setting can only further restrict
        bool showNumbers = m_settings->showZoneNumbers() && (!screenLayout || screenLayout->showZoneNumbers());
        writeQmlProperty(window, QStringLiteral("showNumbers"), showNumbers);
        writeFontProperties(window, m_settings);
    }

    // Update shader-specific properties if using shader overlay
    // Only update if this window is actually a shader overlay window (check isShaderOverlay property)
    const bool windowIsShader = window->property("isShaderOverlay").toBool();
    const bool screenUsesShader = useShaderForScreen(screenId);
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
    QVariantList zones = buildZonesList(screenId, physScreen);
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
        updateLabelsTextureForWindow(window, patched, physScreen, screenLayout);
        // Note: zoneDataVersion is bumped and broadcast to all windows in
        // updateGeometries() after all per-screen updates complete. Do not
        // write it here — updateOverlayWindow() is called per-screen, and
        // writing the version mid-loop would cause inconsistent state across
        // windows (some see the new version, others the old one).
    }
}

} // namespace PlasmaZones
