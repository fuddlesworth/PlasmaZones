// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "internal.h"
#include "../overlayservice.h"
#include "../../core/logging.h"
#include <PhosphorSurfaces/SurfaceManager.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Zone.h>
#include "../../core/geometryutils.h"
#include "../../core/utils.h"
#include <PhosphorScreens/VirtualScreen.h>
#include <PhosphorScreens/Manager.h>
#include "../../core/shaderregistry.h"
#include <QQuickWindow>
#include <QScreen>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QMutexLocker>
#include <QPointer>

#include <PhosphorLayer/ILayerShellTransport.h>
#include <PhosphorLayer/Surface.h>
#include "pz_roles.h"
#include <PhosphorScreens/ScreenIdentity.h>

namespace PlasmaZones {

void OverlayService::destroyIfTypeMismatch(const QString& screenId)
{
    auto it = m_screenStates.find(screenId);
    if (it == m_screenStates.end()) {
        return;
    }
    auto* existing = it->overlayWindow;
    if (!existing) {
        return;
    }
    const bool windowIsShader = existing->property("isShaderOverlay").toBool();
    const bool shouldUseShader = useShaderForScreen(screenId);
    if (windowIsShader != shouldUseShader) {
        destroyOverlayWindow(screenId);
    }
}

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
        cursorEffectiveId = Utils::effectiveScreenIdAt(m_screenManager, pos, cursorScreen);
    } else if (cursorScreen) {
        cursorEffectiveId = Phosphor::Screens::ScreenIdentity::identifierFor(cursorScreen);
    }

    // Store the effective screen ID for cross-virtual-screen detection in showAtPosition()
    m_currentOverlayScreenId = showOnAllMonitors ? QString() : cursorEffectiveId;

    // Phase 0: build the set of target screen ids — the effective ids that
    // should have a live overlay window after this call completes. Filters
    // on single-monitor mode, disabled contexts, autotile exclusion, and
    // physical-screen resolvability.
    auto* mgr = m_screenManager;
    const QStringList effectiveIds = mgr ? mgr->effectiveScreenIds() : QStringList();
    const bool haveEffective = mgr && !effectiveIds.isEmpty();

    // One overlay window per effective screen (all virtual screens across
    // all physical monitors). Keeping every VS's overlay alive means
    // cross-VS switching during or between drags is just a matter of
    // flipping per-window _idled state in applyIdleStateForCursor() —
    // no layer-shell surface re-anchoring, no Vulkan swap chain churn,
    // no ~QQuickWindow stall. The single-monitor filter that used to
    // drop non-cursor VSes was the root cause of "wrong spot" after
    // cross-VS drag: the existing overlay stayed anchored to the
    // original VS's bounds because the rekey path didn't replay
    // configureLayerSurface + updateWindowScreenPosition.
    QStringList targetIds;
    QHash<QString, QScreen*> targetPhysScreens;
    QHash<QString, QRect> targetGeometries;
    if (haveEffective) {
        for (const QString& screenId : effectiveIds) {
            QScreen* physScreen = mgr->physicalQScreenFor(screenId);
            if (!physScreen) {
                continue;
            }
            if (isContextDisabled(m_settings, PhosphorZones::AssignmentEntry::Snapping, screenId,
                                  m_currentVirtualDesktop, m_currentActivity)) {
                continue;
            }
            if (m_excludedScreens.contains(screenId)) {
                continue;
            }
            targetIds.append(screenId);
            targetPhysScreens.insert(screenId, physScreen);
            targetGeometries.insert(screenId, mgr->screenGeometry(screenId));
        }
    } else {
        for (auto* screen : Utils::allScreens()) {
            const QString screenId = Phosphor::Screens::ScreenIdentity::identifierFor(screen);
            if (isContextDisabled(m_settings, PhosphorZones::AssignmentEntry::Snapping, screenId,
                                  m_currentVirtualDesktop, m_currentActivity)) {
                continue;
            }
            if (m_excludedScreens.contains(screenId)) {
                continue;
            }
            targetIds.append(screenId);
            targetPhysScreens.insert(screenId, screen);
            targetGeometries.insert(screenId, screen->geometry());
        }
    }

    const QSet<QString> targetSet(targetIds.cbegin(), targetIds.cend());

    // Phase 1 — REKEY. For every target id that lacks a live overlay window,
    // look for an existing m_screenStates entry under a different key but with
    // the SAME physical monitor. Move (rekey) its state to the target id.
    //
    // This preserves the live QQuickWindow and its VkSwapchainKHR across
    // effective-id "flavor flips" — for example when Utils::effectiveScreenIdAt
    // jitters between "LG..:115107" and "LG..:115107/vs:0" because a VS config
    // entry was added/removed/re-cached mid-session. Before this fix, each
    // flip forced a full Vulkan swap-chain teardown + layer-shell surface
    // reinit, and rapid flips during a drag (via updateDragCursor) stacked
    // on the daemon main thread long enough to starve D-Bus delivery and
    // manifest as the runaway overlay-create loop this refactor is fixing.
    for (const QString& targetId : targetIds) {
        auto it = m_screenStates.constFind(targetId);
        if (it != m_screenStates.constEnd() && it->overlayWindow) {
            continue; // Already correctly keyed with a live window.
        }
        const QString targetPhys = PhosphorIdentity::VirtualScreenId::extractPhysicalId(targetId);
        QString donorKey;
        for (auto sit = m_screenStates.constBegin(); sit != m_screenStates.constEnd(); ++sit) {
            if (targetSet.contains(sit.key())) {
                continue; // Another target's entry, leave it alone.
            }
            if (!sit.value().overlayWindow) {
                continue;
            }
            if (PhosphorIdentity::VirtualScreenId::extractPhysicalId(sit.key()) != targetPhys) {
                continue;
            }
            donorKey = sit.key();
            break;
        }
        if (!donorKey.isEmpty()) {
            rekeyOverlayState(donorKey, targetId);
        }
    }

    // Phase 2 — DISMISS. Every remaining m_screenStates entry whose key is
    // not in targetSet is either a different physical monitor we're switching
    // away from, or a leftover from a removed/excluded screen. Hide
    // non-shader overlays (cheap, no Vulkan churn — mirrors the 9e0cb05f
    // "hide-not-destroy" policy that dismissOverlayWindow(QScreen*) uses)
    // and destroy shader overlays (QSGRenderNode pipelines are bound to the
    // per-window QRhi context, so destroy-on-hide is mandatory there).
    const QStringList allKeys = m_screenStates.keys();
    for (const QString& key : allKeys) {
        if (targetSet.contains(key)) {
            continue;
        }
        dismissOverlayWindow(key);
    }

    // Phase 3 — CREATE & SHOW. For each target id, create a window if we
    // still don't have one (rekey phase didn't find a donor), push current
    // geometry to rekeyed donors, and call show().
    for (const QString& screenId : targetIds) {
        QScreen* physScreen = targetPhysScreens.value(screenId);
        const QRect geom = targetGeometries.value(screenId);
        if (!physScreen) {
            continue;
        }

        destroyIfTypeMismatch(screenId);
        if (!m_screenStates.contains(screenId) || !m_screenStates[screenId].overlayWindow) {
            if (haveEffective) {
                createOverlayWindow(screenId, physScreen, geom);
            } else {
                createOverlayWindow(physScreen);
            }
        }
        if (auto* window = m_screenStates.value(screenId).overlayWindow) {
            m_screenStates[screenId].overlayPhysScreen = physScreen;
            if (geom.isValid()) {
                m_screenStates[screenId].overlayGeometry = geom;
            }
            const QRect storedGeom =
                m_screenStates[screenId].overlayGeometry.isValid() ? m_screenStates[screenId].overlayGeometry : geom;
            assertWindowOnScreen(window, physScreen, storedGeom);
            qCDebug(lcOverlay) << "initializeOverlay: screenId=" << screenId << "geom=" << geom << "windowScreen="
                               << (window->screen() ? window->screen()->name() : QStringLiteral("null"));
            updateOverlayWindow(screenId, physScreen);
            window->show();
            window->update();
        }
    }

    validateScreenStateInvariant(targetIds);

    if (anyScreenUsesShader()) {
        updateZonesForAllWindows(); // Push initial zone data
        startShaderAnimation();
    }

    // With one overlay per VS, every overlay was just show()n above.
    // Apply per-window idle state: in single-monitor mode, only the
    // cursor's VS is un-idled; all others stay idle (content.visible=false,
    // Qt.WindowTransparentForInput set). In showOnAllMonitors mode, all
    // overlays are un-idled simultaneously.
    applyIdleStateForCursor(cursorEffectiveId, showOnAllMonitors);

    Q_EMIT visibilityChanged(true);
}

void OverlayService::updateLayout(PhosphorZones::Layout* layout)
{
    setLayout(layout);
    if (m_visible) {
        updateGeometries();

        // Flash zones to indicate layout change if enabled
        if (m_settings && m_settings->flashZonesOnSwitch()) {
            for (auto it_ = m_screenStates.constBegin(); it_ != m_screenStates.constEnd(); ++it_) {
                auto* window = it_.value().overlayWindow;
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
    for (const QString& screenId : m_screenStates.keys()) {
        QScreen* physScreen = m_screenStates.value(screenId).overlayPhysScreen;
        if (physScreen) {
            updateOverlayWindow(screenId, physScreen);
        }
    }
    // Geometry data is now current — do NOT bump version here.
    // updateZonesForAllWindows() is the single authoritative version bump point.
}

void OverlayService::highlightZone(const QString& zoneId)
{
    // Mark zone data dirty for shader overlay updates
    m_zoneDataDirty = true;

    // Update the highlightedZoneId property on all overlay windows
    for (auto it_ = m_screenStates.constBegin(); it_ != m_screenStates.constEnd(); ++it_) {
        auto* window = it_.value().overlayWindow;
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

    for (auto it_ = m_screenStates.constBegin(); it_ != m_screenStates.constEnd(); ++it_) {
        auto* window = it_.value().overlayWindow;
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
    for (auto it_ = m_screenStates.constBegin(); it_ != m_screenStates.constEnd(); ++it_) {
        auto* window = it_.value().overlayWindow;
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
    // Use per-screen overlayGeometry for coordinate translation instead of
    // window->mapFromGlobal(), which may return wrong coordinates before the
    // first frame because QWindow::geometry() doesn't reflect LayerShell
    // positioning yet.
    for (auto it = m_screenStates.constBegin(); it != m_screenStates.constEnd(); ++it) {
        if (it.value().overlayWindow) {
            const QRect targetGeom = it.value().overlayGeometry;
            if (!targetGeom.isValid()) {
                // No overlay geometry recorded for this screen ID yet. Skip rather than
                // falling back to QWindow::mapFromGlobal(), which is unreliable on Wayland
                // with LayerShell until after the first frame (geometry not yet applied).
                qCWarning(lcOverlay) << "updateMousePosition: no overlay geometry for screen" << it.key()
                                     << "— skipping mouse position update";
                continue;
            }
            const QPointF local(cursorX - targetGeom.x(), cursorY - targetGeom.y());
            it.value().overlayWindow->setProperty("mousePosition", local);
        }
    }
}

void OverlayService::createOverlayWindow(QScreen* screen)
{
    const QString screenId = Phosphor::Screens::ScreenIdentity::identifierFor(screen);
    auto* mgr = m_screenManager;
    QRect geom = (mgr && mgr->screenGeometry(screenId).isValid()) ? mgr->screenGeometry(screenId) : screen->geometry();
    createOverlayWindow(screenId, screen, geom);
}

void OverlayService::createOverlayWindow(const QString& screenId, QScreen* physScreen, const QRect& geometry)
{
    if (m_screenStates.contains(screenId) && m_screenStates[screenId].overlaySurface) {
        return;
    }

    // Choose overlay type based on shader settings for THIS screen's layout
    bool usingShader = useShaderForScreen(screenId);

    // Compute virtual-screen overrides up-front (wlr-layer-shell locks
    // output+anchors at attach).
    const QRect physScreenGeom = physScreen ? physScreen->geometry() : geometry;
    const bool isVS = PhosphorIdentity::VirtualScreenId::isVirtual(screenId);
    const auto placement = layerPlacementForVs(isVS ? geometry : QRect(), physScreenGeom);
    std::optional<PhosphorLayer::Anchors> anchorsOverride(placement.anchors);
    std::optional<QMargins> marginsOverride;
    if (!placement.margins.isNull()) {
        marginsOverride = placement.margins;
    }

    const auto role = PzRoles::Overlay.withScopePrefix(
        QStringLiteral("plasmazones-overlay-%1-%2").arg(screenId).arg(m_surfaceManager->nextScopeGeneration()));

    // Try shader overlay first, fall back to standard overlay if it fails.
    QVariantMap initProps;
    if (usingShader) {
        // Set labelsTexture before QML loads so ZoneShaderItem binding never sees undefined.
        QImage placeholder(1, 1, QImage::Format_ARGB32);
        placeholder.fill(Qt::transparent);
        initProps.insert(QStringLiteral("labelsTexture"), QVariant::fromValue(placeholder));
    }
    initProps.insert(QStringLiteral("isShaderOverlay"), usingShader);

    PhosphorLayer::Surface* surface = nullptr;
    if (usingShader) {
        surface = createLayerSurface({.qmlUrl = QUrl(QStringLiteral("qrc:/ui/RenderNodeOverlay.qml")),
                                      .screen = physScreen,
                                      .role = role,
                                      .windowType = "shader overlay",
                                      .windowProperties = initProps,
                                      .anchorsOverride = anchorsOverride,
                                      .marginsOverride = marginsOverride});
        if (surface) {
            qCInfo(lcOverlay) << "Overlay window created: RenderNodeOverlay (ZoneShaderItem) for screen" << screenId;
        } else {
            qCWarning(lcOverlay) << "Falling back to standard overlay";
            usingShader = false;
            initProps.insert(QStringLiteral("isShaderOverlay"), false);
        }
    }
    if (!surface) {
        surface = createLayerSurface({.qmlUrl = QUrl(QStringLiteral("qrc:/ui/ZoneOverlay.qml")),
                                      .screen = physScreen,
                                      .role = role,
                                      .windowType = "overlay",
                                      .windowProperties = initProps,
                                      .anchorsOverride = anchorsOverride,
                                      .marginsOverride = marginsOverride});
        if (!surface) {
            return;
        }
    }
    auto* window = surface->window();

    // Set window size to cover the target screen area (physical or virtual).
    // Position is controlled by layer-surface anchors + margins for virtual screens,
    // so setX/setY are only used as hints for mapFromGlobal.
    window->setWidth(geometry.width());
    window->setHeight(geometry.height());

    // Set shader-specific properties (use QQmlProperty so QML bindings see updates)
    // Use per-screen layout (same resolution as updateOverlayWindow) so each monitor
    // gets the correct shader when per-screen assignments differ.
    PhosphorZones::Layout* screenLayout = resolveScreenLayout(screenId);

    if (usingShader && screenLayout) {
        auto* registry = m_shaderRegistry;
        if (registry) {
            const QString shaderId = screenLayout->shaderId();
            const ShaderRegistry::ShaderInfo info = registry->shader(shaderId);
            qCDebug(lcOverlay) << "Overlay shader=" << shaderId << "multipass=" << info.isMultipass
                               << "bufferPaths=" << info.bufferShaderPaths.size();
            QVariantMap translatedParams = registry->translateParamsToUniforms(shaderId, screenLayout->shaderParams());
            applyShaderInfoToWindow(window, info, translatedParams, geometry, physScreenGeom);
        }
    }

    // Connect to physical screen geometry changes. Shared helper keeps the
    // lambda body DRY with rekeyOverlayState's re-connect path.
    QMetaObject::Connection geomConn = installOverlayGeometryWatcher(physScreen, screenId, isVS);

    if (usingShader) {
        writeQmlProperty(window, QStringLiteral("zoneDataVersion"), m_zoneDataVersion);
    }

    auto& stateRef = m_screenStates[screenId];
    stateRef.overlaySurface = surface;
    stateRef.overlayWindow = window;
    stateRef.overlayPhysScreen = physScreen;
    stateRef.overlayGeometry = geometry;
    stateRef.overlayGeomConnection = geomConn;
}

void OverlayService::recreateOverlayWindowsOnTypeMismatch()
{
    QStringList screensToRecreate;
    for (auto it = m_screenStates.constBegin(); it != m_screenStates.constEnd(); ++it) {
        auto* window = it.value().overlayWindow;
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
    // Snapshot per-screen state before destroying
    QHash<QString, QScreen*> savedPhysScreens;
    QHash<QString, QRect> savedGeometries;
    for (auto it_ = m_screenStates.constBegin(); it_ != m_screenStates.constEnd(); ++it_) {
        savedPhysScreens.insert(it_.key(), it_.value().overlayPhysScreen);
        savedGeometries.insert(it_.key(), it_.value().overlayGeometry);
    }

    for (const QString& screenId : screensToRecreate)
        destroyOverlayWindow(screenId);
    for (const QString& screenId : screensToRecreate) {
        if (!isContextDisabled(m_settings, PhosphorZones::AssignmentEntry::Snapping, screenId, m_currentVirtualDesktop,
                               m_currentActivity)) {
            QScreen* physScreen = savedPhysScreens.value(screenId);
            if (!physScreen)
                continue;
            const QRect geom = savedGeometries.value(screenId, physScreen->geometry());
            createOverlayWindow(screenId, physScreen, geom);
            if (auto* w = m_screenStates.value(screenId).overlayWindow) {
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

void OverlayService::dismissOverlayWindow(QScreen* screen)
{
    const QString physId = Phosphor::Screens::ScreenIdentity::identifierFor(screen);

    // Collect matching overlay keys — may be virtual screen IDs for this physical screen
    QStringList matchingKeys;
    for (auto it = m_screenStates.constBegin(); it != m_screenStates.constEnd(); ++it) {
        if (PhosphorIdentity::VirtualScreenId::extractPhysicalId(it.key()) == physId) {
            matchingKeys.append(it.key());
        }
    }

    for (const QString& screenId : matchingKeys) {
        dismissOverlayWindow(screenId);
    }
}

void OverlayService::dismissOverlayWindow(const QString& screenId)
{
    auto it = m_screenStates.find(screenId);
    if (it == m_screenStates.end()) {
        return;
    }
    auto* window = it->overlayWindow;
    if (!window) {
        return;
    }
    // Shader overlays: must destroy — QSGRenderNode Vulkan pipelines are tied
    // to the per-window QRhi which gets invalidated when hide() tears down the
    // wl_surface and show() creates a new one.
    // Non-shader overlays: hide() is safe — standard QML items recover from
    // scene graph pause/resume, avoiding Vulkan surface create/destroy churn.
    if (window->property("isShaderOverlay").toBool()) {
        destroyOverlayWindow(screenId);
    } else {
        window->hide();
    }
}

bool OverlayService::rekeyOverlayState(const QString& oldKey, const QString& newKey)
{
    if (oldKey == newKey) {
        return false;
    }
    auto donor = m_screenStates.find(oldKey);
    if (donor == m_screenStates.end() || !donor->overlayWindow) {
        return false;
    }

    // Rekey is only valid when the surface's protocol-level placement
    // (anchors, in particular) does not have to change. wlr-layer-shell
    // advertises v2+ `set_anchor` as mutable, but several compositors
    // (weston, some mutter forks) silently ignore post-attach anchor
    // changes and the surface stays pinned to its original anchors. A
    // physical→virtual (or VS→VS with different anchor set) flip requires
    // AnchorAll → Top|Left — if that anchor change no-ops on the donor
    // compositor, the overlay keeps rendering across the wrong region.
    // Bail out and let the caller destroy+recreate when the anchor set
    // would have to change.
    const bool wasVS = PhosphorIdentity::VirtualScreenId::isVirtual(oldKey);
    const bool willBeVS = PhosphorIdentity::VirtualScreenId::isVirtual(newKey);
    if (wasVS != willBeVS) {
        qCInfo(lcOverlay) << "rekeyOverlayState: refusing flavor flip rekey" << oldKey << "->" << newKey
                          << "(anchors would change; some compositors ignore post-attach set_anchor)";
        return false;
    }

    // If a stale (empty) entry already exists under newKey, drop it so the
    // move lands cleanly. It has no live window — if it did the caller
    // should not have selected this donor.
    auto existing = m_screenStates.find(newKey);
    if (existing != m_screenStates.end()) {
        if (existing->overlayWindow) {
            qCWarning(lcOverlay) << "rekeyOverlayState: refusing to clobber live entry under" << newKey << "with donor"
                                 << oldKey;
            return false;
        }
        m_screenStates.erase(existing);
    }
    PerScreenOverlayState state = std::move(donor.value());
    m_screenStates.erase(donor);
    auto inserted = m_screenStates.insert(newKey, std::move(state));

    // The geometryChanged lambda captured the OLD sid by value. After the
    // state moved to newKey, the lambda's m_screenStates.find(oldSid) lookup
    // would return end() and silently drop every subsequent geometry update.
    // Rebuild the connection with the new key so live resizes keep reaching
    // the overlay.
    auto& rekeyed = inserted.value();
    if (rekeyed.overlayGeomConnection) {
        QObject::disconnect(rekeyed.overlayGeomConnection);
        rekeyed.overlayGeomConnection = {};
    }
    QScreen* physScreen = rekeyed.overlayPhysScreen;
    if (physScreen) {
        const bool isVS = PhosphorIdentity::VirtualScreenId::isVirtual(newKey);

        // Re-anchor the live layer surface to the new VS's region. The donor's
        // anchors/margins were baked in at attach time for the old key — if the
        // flavor flip changes the target geometry (e.g. bare-physical donor
        // rekeyed to a sub-region VS target) the surface would otherwise keep
        // rendering across the full monitor. wlr-layer-shell v2+ allows
        // set_anchor / set_margin post-attach; push the corrected placement
        // through the mutable transport handle.
        if (rekeyed.overlaySurface) {
            if (auto* handle = rekeyed.overlaySurface->transport()) {
                const QRect targetVsGeom = resolveScreenGeometry(m_screenManager, newKey);
                const auto placement = layerPlacementForVs(isVS ? targetVsGeom : QRect(), physScreen->geometry());
                handle->setAnchors(placement.anchors);
                handle->setMargins(placement.margins);
                if (isVS && targetVsGeom.isValid()) {
                    rekeyed.overlayGeometry = targetVsGeom;
                    if (auto* w = rekeyed.overlayWindow) {
                        w->setWidth(targetVsGeom.width());
                        w->setHeight(targetVsGeom.height());
                    }
                }
            }
        }

        rekeyed.overlayGeomConnection = installOverlayGeometryWatcher(physScreen, newKey, isVS);
    }

    qCInfo(lcOverlay) << "rekeyOverlayState: migrated overlay" << oldKey << "->" << newKey
                      << "(same physical monitor, preserving Vulkan surface)";
    return true;
}

void OverlayService::validateScreenStateInvariant(const QStringList& targetIds) const
{
#ifndef QT_NO_DEBUG
    // Invariant (one-overlay-per-VS): every live overlay's key must be in
    // targetIds. Phase 2 dismisses any stale entries and Phase 3 creates
    // missing targets, so by the end of initializeOverlay every live
    // m_screenStates entry should correspond to an enabled effective
    // screen id. Multiple live entries per physical monitor are NOT a
    // violation in this model — two virtual screens sharing one physical
    // monitor each own their own overlay window, and that's the whole
    // point of the one-per-VS refactor.
    const QSet<QString> targetSet(targetIds.cbegin(), targetIds.cend());
    for (auto it = m_screenStates.constBegin(); it != m_screenStates.constEnd(); ++it) {
        if (!it.value().overlayWindow) {
            continue;
        }
        if (!targetSet.contains(it.key())) {
            qCWarning(lcOverlay) << "validateScreenStateInvariant: live overlay" << it.key()
                                 << "is not in the current target set — orphan";
            Q_ASSERT_X(false, "OverlayService", "orphaned overlay entry");
        }
    }
#else
    Q_UNUSED(targetIds);
#endif
}

QMetaObject::Connection OverlayService::installOverlayGeometryWatcher(QScreen* physScreen, const QString& screenId,
                                                                      bool isVS)
{
    if (!physScreen) {
        return {};
    }
    QPointer<QScreen> screenPtr = physScreen;
    const QString sid = screenId; // Capture by value — survives rekey.
    return connect(physScreen, &QScreen::geometryChanged, this, [this, screenPtr, sid, isVS](const QRect& newGeom) {
        if (!screenPtr) {
            return;
        }
        auto stateIt = m_screenStates.find(sid);
        if (stateIt == m_screenStates.end()) {
            return; // State was cleaned up, ignore stale geometry signal
        }
        auto& st = stateIt.value();
        if (auto* w = st.overlayWindow) {
            if (isVS) {
                // Virtual screen: recompute sub-region geometry from Phosphor::Screens::ScreenManager
                // (virtual proportions are relative to the physical screen) and
                // push new margins via the PhosphorLayer transport handle.
                // Anchors (Top|Left) are fixed at attach and can't change.
                const QRect vsGeom = resolveScreenGeometry(m_screenManager, sid);
                if (vsGeom.isValid() && st.overlaySurface) {
                    if (auto* handle = st.overlaySurface->transport()) {
                        handle->setMargins(layerPlacementForVs(vsGeom, newGeom).margins);
                    }
                    w->setWidth(vsGeom.width());
                    w->setHeight(vsGeom.height());
                    st.overlayGeometry = vsGeom;
                    updateOverlayWindow(sid, screenPtr);
                    return;
                }
            } else {
                // Physical screen: AnchorAll auto-sizes to the screen; just
                // mirror the resize to our cached state.
                w->setWidth(newGeom.width());
                w->setHeight(newGeom.height());
                st.overlayGeometry = newGeom;
                updateOverlayWindow(sid, screenPtr);
            }
        }
    });
}

void OverlayService::destroyOverlayWindow(QScreen* screen)
{
    const QString screenId = Phosphor::Screens::ScreenIdentity::identifierFor(screen);
    destroyOverlayWindow(screenId);
}

void OverlayService::destroyOverlayWindow(const QString& screenId)
{
    auto it = m_screenStates.find(screenId);
    if (it == m_screenStates.end()) {
        return;
    }
    // Disconnect the stored geometryChanged connection specifically,
    // rather than disconnecting all signals from the screen to a receiver.
    // The connection targets `this` (not `window`), so the old blanket
    // disconnect(screen, nullptr, window, nullptr) missed it entirely.
    QObject::disconnect(it->overlayGeomConnection);
    if (it->overlaySurface) {
        it->overlaySurface->deleteLater();
        it->overlaySurface = nullptr;
        it->overlayWindow = nullptr;
    }
    it->overlayPhysScreen = nullptr;
    it->overlayGeometry = QRect();
    it->overlayGeomConnection = {};
    // Fresh QQuickWindow on next createOverlayWindow needs a fresh cache.
    it->labelsTextureHash = 0;

    // Erase the entry entirely if nothing else is keeping it alive. Per-screen
    // state carries three surface pointers post-Phase-2 (overlay, zone selector,
    // unified notification — the latter hosts both layout-OSD and nav-OSD
    // content); only the overlay is cleared here. If the others are all null
    // too, the entry is an empty husk that accumulates under hide()+show()
    // cycles. destroyAllWindowsForPhysicalScreen already erases, but hide()
    // used to leave husks behind — drop them here symmetrically.
    if (!it->overlaySurface && !it->zoneSelectorSurface && !it->notificationSurface) {
        m_screenStates.erase(it);
    }
}

void OverlayService::updateOverlayWindow(QScreen* screen)
{
    const QString screenId = Phosphor::Screens::ScreenIdentity::identifierFor(screen);
    updateOverlayWindow(screenId, screen);
}

void OverlayService::updateOverlayWindow(const QString& screenId, QScreen* physScreen)
{
    auto* window = m_screenStates.value(screenId).overlayWindow;
    if (!window) {
        return;
    }

    // Get the layout for this screen to use layout-specific settings
    // Prefer per-screen assignment, fall back to global active layout
    PhosphorZones::Layout* screenLayout = resolveScreenLayout(screenId);

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
        auto* registry = m_shaderRegistry;
        if (registry) {
            const QString shaderId = screenLayout->shaderId();
            const ShaderRegistry::ShaderInfo info = registry->shader(shaderId);
            QVariantMap translatedParams = registry->translateParamsToUniforms(shaderId, screenLayout->shaderParams());
            const QRect vsGeom = resolveScreenGeometry(m_screenManager, screenId);
            const QRect physGeom = physScreen ? physScreen->geometry() : vsGeom;
            applyShaderInfoToWindow(window, info, translatedParams, vsGeom, physGeom);
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
        if (z.toMap().value(::PhosphorZones::ZoneJsonKeys::OverlayDisplayMode).toInt() == 1) {
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
