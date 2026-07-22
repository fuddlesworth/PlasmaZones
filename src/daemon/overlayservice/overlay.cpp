// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "internal.h"
#include "daemon/overlayservice.h"
#include "qml_property_names.h"
#include "core/platform/logging.h"
#include "phosphor_slot_keys.h"
#include <PhosphorOverlay/ShellHost.h>
#include <PhosphorSurfaces/SurfaceManager.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Zone.h>
#include "core/utils/geometryutils.h"
#include "core/utils/utils.h"
#include <PhosphorScreens/VirtualScreen.h>
#include <PhosphorScreens/Manager.h>
#include "core/interfaces/shaderregistry.h"
#include <QQuickWindow>
#include <QScreen>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QImage>
#include <QMutexLocker>
#include <QPointer>

#include <PhosphorLayer/ILayerShellTransport.h>
#include <PhosphorLayer/Surface.h>
#include <PhosphorAnimation/SurfaceAnimator.h>
#include "phosphor_roles.h"
#include <PhosphorScreens/ScreenIdentity.h>

namespace PlasmaZones {

namespace {

// Collapse a dismissed overlay slot's labels texture to a 1x1 placeholder so
// the labels payload (the sparse glyph-tile ZoneLabelTexture) is released while
// the overlay is hidden rather than pinned on the persistent slot property for
// the whole session. The wallpaperTexture
// is reset for symmetry and to drop the slot's stale reference across a
// wallpaper change, but the bulk wallpaper image is owned by ShaderRegistry's
// static cache (s_cachedWallpaperImage / crops) and is COW-shared, so that
// reset reclaims little RSS on its own - the labels release is the real win.
// The next show() rebuilds both via createOverlayWindow ->
// updateLabelsTextureForWindow / applyShaderInfoToWindow. Callers MUST also
// reset PerScreenOverlayState::labelsTextureHash to 0 so the hash compare in
// updateLabelsTextureForWindow does not short-circuit the rebuild and leave
// the 1x1 placeholder showing with no labels.
void releaseOverlaySlotTextures(QQuickItem* slot)
{
    if (!slot) {
        return;
    }
    QImage placeholder(1, 1, QImage::Format_ARGB32);
    placeholder.fill(Qt::transparent);
    const QVariant placeholderVar = QVariant::fromValue(placeholder);
    writeQmlProperty(slot, QStringLiteral("labelsTexture"), placeholderVar);
    writeQmlProperty(slot, QStringLiteral("wallpaperTexture"), placeholderVar);
}

} // namespace

void OverlayService::destroyIfTypeMismatch(const QString& screenId)
{
    auto it = m_screenStates.find(screenId);
    if (it == m_screenStates.end() || !it->overlayPhysScreen) {
        return;
    }
    auto* slot = it->mainOverlaySlot();
    if (!slot) {
        return;
    }
    const bool slotIsShader = slot->property("useShader").toBool();
    const bool shouldUseShader = useShaderForScreen(screenId);
    if (slotIsShader != shouldUseShader) {
        destroyOverlayWindow(screenId);
    }
}

void OverlayService::initializeOverlay(QScreen* cursorScreen, const QPoint& cursorPos)
{
    // Determine if we should show on all monitors (cursorScreen == nullptr means all)
    const bool showOnAllMonitors = (cursorScreen == nullptr);

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
        cursorEffectiveId = PhosphorScreens::ScreenIdentity::identifierFor(cursorScreen);
    }

    // Store the effective screen ID for cross-virtual-screen detection in showAtPosition()
    m_currentOverlayScreenId = showOnAllMonitors ? QString() : cursorEffectiveId;

    // Phase 0: build the set of target screen ids - the effective ids that
    // should have a live overlay window after this call completes. Filters
    // on single-monitor mode, disabled contexts, autotile exclusion, and
    // physical-screen resolvability.
    auto* mgr = m_screenManager;
    const QStringList effectiveIds = mgr ? mgr->effectiveScreenIds() : QStringList();
    const bool haveEffective = mgr && !effectiveIds.isEmpty();

    // One overlay window per effective screen (all virtual screens across
    // all physical monitors). Keeping every VS's overlay alive means
    // cross-VS switching during or between drags is just a matter of
    // flipping per-window _idled state in applyIdleStateForCursor() -
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
            const PhosphorScreens::PhysicalScreen phys = mgr->physicalScreenFor(screenId);
            QScreen* physScreen = phys.qscreen;
            if (!physScreen) {
                continue;
            }
            if (isSnappingContextInactive(screenId)) {
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
            const QString screenId = PhosphorScreens::ScreenIdentity::identifierFor(screen);
            if (isSnappingContextInactive(screenId)) {
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

    // Phase 1 - REKEY. For every target id that lacks a live overlay window,
    // look for an existing m_screenStates entry under a different key but with
    // the SAME physical monitor. Move (rekey) its state to the target id.
    //
    // This preserves the live QQuickWindow and its VkSwapchainKHR across
    // effective-id "flavor flips" - for example when Utils::effectiveScreenIdAt
    // jitters between "LG..:115107" and "LG..:115107/vs:0" because a VS config
    // entry was added/removed/re-cached mid-session. Before this fix, each
    // flip forced a full Vulkan swap-chain teardown + layer-shell surface
    // reinit, and rapid flips during a drag (via updateDragCursor) stacked
    // on the daemon main thread long enough to starve D-Bus delivery and
    // manifest as the runaway overlay-create loop this refactor is fixing.
    for (const QString& targetId : targetIds) {
        auto it = m_screenStates.constFind(targetId);
        if (it != m_screenStates.constEnd() && it->overlayPhysScreen) {
            continue; // Already correctly keyed with a live overlay context.
        }
        const QString targetPhys = PhosphorIdentity::VirtualScreenId::extractPhysicalId(targetId);
        QString donorKey;
        for (auto sit = m_screenStates.constBegin(); sit != m_screenStates.constEnd(); ++sit) {
            if (targetSet.contains(sit.key())) {
                continue; // Another target's entry, leave it alone.
            }
            if (!sit.value().overlayPhysScreen) {
                continue;
            }
            if (PhosphorIdentity::VirtualScreenId::extractPhysicalId(sit.key()) != targetPhys) {
                continue;
            }
            donorKey = sit.key();
            break;
        }
        if (!donorKey.isEmpty()) {
            if (!rekeyOverlayState(donorKey, targetId)) {
                // Refused (flavor flip, live target, or lib rejection).
                // Phase-2 dismiss + Phase-3 create below recover by
                // building a fresh window under targetId - log so the
                // perf regression (full Vulkan teardown vs preserved
                // swapchain) is visible in field reports.
                qCDebug(lcOverlay) << "initializeOverlay: rekey refused" << donorKey << "->" << targetId
                                   << "- falling back to dismiss+recreate";
            }
        }
    }

    // Phase 2 - DISMISS. Every remaining m_screenStates entry whose key is
    // not in targetSet is either a different physical monitor we're switching
    // away from, or a leftover from a removed/excluded screen. Hide
    // non-shader overlays (cheap, no Vulkan churn - mirrors the 9e0cb05f
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

    // Phase 3 - CREATE & SHOW. For each target id, create a window if we
    // still don't have one (rekey phase didn't find a donor), push current
    // geometry to rekeyed donors, and call show().
    for (const QString& screenId : targetIds) {
        QScreen* physScreen = targetPhysScreens.value(screenId);
        const QRect geom = targetGeometries.value(screenId);
        if (!physScreen) {
            continue;
        }

        destroyIfTypeMismatch(screenId);
        if (!m_screenStates.contains(screenId) || !m_screenStates[screenId].overlayPhysScreen) {
            if (haveEffective) {
                createOverlayWindow(screenId, physScreen, geom);
            } else {
                createOverlayWindow(physScreen);
            }
        }
        auto* shellState = m_screenStates.value(screenId).shell;
        if (auto* window = shellState ? shellState->shellWindow() : nullptr) {
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
            // Post-shell-migration: the shell window is kept mapped across
            // hides while shaders or animations are enabled (effects-gated
            // keepMappedOnHide, see createWarmedOsdSurface); animation drives
            // the per-content slot's opacity. Surface::show() only fires on
            // a Hidden→Shown transition (once per daemon lifetime with
            // effects on; per re-show after an unmap with effects off).
            auto* shellSurface = shellState->shellSurface();
            auto* slot = m_screenStates[screenId].mainOverlaySlot();
            if (shellSurface && slot) {
                if (!shellSurface->isLogicallyShown()) {
                    shellSurface->show();
                }
                slot->setVisible(true);
                m_surfaceAnimator->beginShow(shellSurface, slot, PhosphorRoles::ZoneOverlay, []() { });
                // Main overlay during drag is purely visual (KWin owns
                // the drag, daemon receives cursor pushes via D-Bus).
                // Sync to keep the surface click-through unless a
                // sibling modal slot is also up.
                syncPassiveShellSurfaceState(screenId);
            }
            window->update();
        }
    }

    validateScreenStateInvariant(targetIds);

    // Count how many overlay windows actually have a live shell surface.
    // If zero, the transport (phosphorwayland) is unavailable and we must
    // not mark ourselves visible - the caller (e.g. prepareHandlerContext)
    // will retry on the next drag tick, and handleScreenAdded will also
    // attempt recreation on screen reconnection.
    int liveOverlayCount = 0;
    for (const auto& state : m_screenStates) {
        if (state.overlayPhysScreen && state.shell && state.shell->shellWindow()) {
            ++liveOverlayCount;
        }
    }

    if (liveOverlayCount == 0) {
        qCWarning(lcOverlay) << "initializeOverlay: no overlay windows created: "
                                "phosphorwayland transport unavailable "
                                "(overlays disabled on this screen)";
        m_visible = false;
        return;
    }

    m_visible = true;
    m_overlayIdled = false; // a fresh show is displaying content

    // Spin up the audio-visualizer capture now that the overlay is displaying
    // (no-op if audio-viz is disabled). syncCavaState gates on isOverlayDisplaying.
    syncCavaState();

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
        // Apply the new layout to the windows even while warm-idled:
        // updateGeometries() → updateOverlayWindow() re-applies each window's
        // shader source/params + geometry, which MUST be current for the next
        // refreshFromIdle() resume — refreshFromIdle() re-pushes zones but NOT
        // shader info, so deferring this would leave the previous shader
        // rendering after a mid-idle active-layout switch. The zone data
        // updateOverlayWindow also writes is hidden by _idled while idled and
        // re-pushed by refreshFromIdle() on resume, and updateGeometries() does
        // not set m_zoneDataDirty, so the drag-pause blank is preserved.
        updateGeometries();

        // Flash zones to indicate layout change if enabled
        if (m_settings && m_settings->flashZonesOnSwitch()) {
            for (auto it_ = m_screenStates.constBegin(); it_ != m_screenStates.constEnd(); ++it_) {
                if (!it_.value().overlayPhysScreen) {
                    continue;
                }
                auto* slot = it_.value().mainOverlaySlot();
                if (slot) {
                    QMetaObject::invokeMethod(slot, "flash");
                }
            }
        }

        // Shader state management - MUST be outside flashZonesOnSwitch block
        // to ensure shader animations work regardless of flash setting.
        // Gate the render-loop restart on isOverlayDisplaying(): while warm-idled
        // a layout switch must NOT start the 60 Hz loop or re-push zones — that
        // would undo the idle quiesce and un-blank the overlay. refreshFromIdle()
        // restarts the loop and re-pushes zones on resume.
        if (anyScreenUsesShader()) {
            if (isOverlayDisplaying()) {
                // Ensure shader timing + updates continue after layout switch
                ensureShaderTimerStarted(m_shaderTimer, m_shaderTimerMutex, m_lastFrameTime, m_frameCount);
                m_zoneDataDirty = true;
                updateZonesForAllWindows();
                if (!m_shaderUpdateTimer || !m_shaderUpdateTimer->isActive()) {
                    startShaderAnimation();
                }
            }
        } else {
            stopShaderAnimation();
        }
    }
}

void OverlayService::updateGeometries()
{
    // Iterate via constBegin/constEnd rather than `.keys()` — the prior
    // shape allocated a QStringList copy on every geometry update; this
    // is a hot path during multi-monitor compositor signal storms (Plasma
    // emits screenAdded/screenRemoved/geometryChanged in tight bursts on
    // hotplug and DPMS-wake). updateOverlayWindow does not mutate
    // m_screenStates, so iterating in-place is safe.
    for (auto it = m_screenStates.constBegin(); it != m_screenStates.constEnd(); ++it) {
        QScreen* physScreen = it.value().overlayPhysScreen;
        if (physScreen) {
            updateOverlayWindow(it.key(), physScreen);
        }
    }
    // Geometry data is now current - do NOT bump version here.
    // updateZonesForAllWindows() is the single authoritative version bump point.
}

void OverlayService::highlightZone(const QString& zoneId)
{
    // Mark zone data dirty for shader overlay updates
    m_zoneDataDirty = true;

    for (auto it_ = m_screenStates.constBegin(); it_ != m_screenStates.constEnd(); ++it_) {
        auto* slot = it_.value().mainOverlaySlot();
        if (slot) {
            writeQmlProperty(slot, QStringLiteral("highlightedZoneId"), zoneId);
            writeQmlProperty(slot, QStringLiteral("highlightedZoneIds"), QVariantList());
        }
    }
}

void OverlayService::highlightZones(const QStringList& zoneIds)
{
    m_zoneDataDirty = true;

    QVariantList zoneIdList;
    for (const QString& zoneId : zoneIds) {
        zoneIdList.append(zoneId);
    }

    for (auto it_ = m_screenStates.constBegin(); it_ != m_screenStates.constEnd(); ++it_) {
        auto* slot = it_.value().mainOverlaySlot();
        if (slot) {
            writeQmlProperty(slot, QStringLiteral("highlightedZoneIds"), zoneIdList);
            writeQmlProperty(slot, QStringLiteral("highlightedZoneId"), QString());
        }
    }
}

void OverlayService::clearHighlight()
{
    m_zoneDataDirty = true;

    for (auto it_ = m_screenStates.constBegin(); it_ != m_screenStates.constEnd(); ++it_) {
        auto* slot = it_.value().mainOverlaySlot();
        if (slot) {
            writeQmlProperty(slot, QStringLiteral("highlightedZoneId"), QString());
            writeQmlProperty(slot, QStringLiteral("highlightedZoneIds"), QVariantList());
        }
    }
}

void OverlayService::updateMousePosition(int cursorX, int cursorY)
{
    if (!m_visible) {
        return;
    }

    for (auto it = m_screenStates.constBegin(); it != m_screenStates.constEnd(); ++it) {
        if (it.value().mainOverlaySlot()) {
            const QRect targetGeom = it.value().overlayGeometry;
            if (!targetGeom.isValid()) {
                // Expected-transient: during a virtual-screen reconfigure an
                // overlay slot can exist for a beat before its geometry is
                // resolved. updateMousePosition runs once per cursor-move
                // event (~30 Hz), so warning here floods the journal for a
                // condition that self-heals on the next geometry update.
                qCDebug(lcOverlay) << "updateMousePosition: no overlay geometry for screen" << it.key()
                                   << ": skipping mouse position update";
                continue;
            }
            const QPointF local(cursorX - targetGeom.x(), cursorY - targetGeom.y());
            it.value().mainOverlaySlot()->setProperty("mousePosition", local);
        }
    }
}

void OverlayService::createOverlayWindow(QScreen* screen)
{
    const QString screenId = PhosphorScreens::ScreenIdentity::identifierFor(screen);
    auto* mgr = m_screenManager;
    QRect geom = (mgr && mgr->screenGeometry(screenId).isValid()) ? mgr->screenGeometry(screenId) : screen->geometry();
    createOverlayWindow(screenId, screen, geom);
}

void OverlayService::createOverlayWindow(const QString& screenId, QScreen* physScreen, const QRect& geometry)
{
    // Post-shell-migration: the per-screen PhosphorRoles::ZoneOverlay wl_surface
    // is replaced by an Item slot inside the per-screen passive shell.
    // Both overlay modes (rectangles + shader) live as alternative
    // sourceComponents inside the same slot, switched via the slot's
    // `useShader` property.
    if (!physScreen) {
        qCWarning(lcOverlay) << "createOverlayWindow: null physScreen for screen=" << screenId;
        return;
    }
    auto* state = ensurePassiveShellFor(screenId, physScreen);
    if (!state || !state->shell || !state->mainOverlaySlot()) {
        return;
    }

    bool usingShader = useShaderForScreen(screenId);
    const QRect physScreenGeom = physScreen ? physScreen->geometry() : geometry;
    const bool isVS = PhosphorIdentity::VirtualScreenId::isVirtual(screenId);

    auto* slot = state->mainOverlaySlot();
    auto* window = state->shell->shellWindow();

    state->overlayPhysScreen = physScreen;
    state->overlayGeometry = geometry;

    // Drive the slot's mode flag - flips between ZoneOverlayContent and
    // RenderNodeOverlayContent. Loader.sourceComponent re-evaluates and
    // mounts the correct content body.
    if (usingShader) {
        QImage placeholder(1, 1, QImage::Format_ARGB32);
        placeholder.fill(Qt::transparent);
        writeQmlProperty(slot, QStringLiteral("labelsTexture"), QVariant::fromValue(placeholder));
    }
    writeQmlProperty(slot, QStringLiteral("useShader"), usingShader);
    writeQmlProperty(slot, QStringLiteral("loaded"), false);
    writeQmlProperty(slot, QStringLiteral("loaded"), true);

    if (window) {
        window->setWidth(geometry.width());
        window->setHeight(geometry.height());
    }

    PhosphorZones::Layout* screenLayout = resolveScreenLayout(screenId);
    if (usingShader && screenLayout) {
        auto* registry = m_shaderRegistry;
        if (registry) {
            // A context overlay rule may override the layout's shader (with
            // optional uniform params). When the rule sets the shader, use its
            // params — an override with no params falls back to the shader's
            // defaults; otherwise use the layout's params.
            const PhosphorZones::ContextOverlayOverride overlayOverride =
                overlayOverrideForScreen(m_layoutManager, screenId);
            const QString shaderId = overlayOverride.shaderId.value_or(screenLayout->shaderId());
            const QVariantMap rawParams =
                overlayOverride.shaderId ? overlayOverride.shaderParams : screenLayout->shaderParams();
            const ShaderRegistry::ShaderInfo info = registry->shader(shaderId);
            qCDebug(lcOverlay) << "Overlay shader=" << shaderId << "multipass=" << info.isMultipass
                               << "bufferPaths=" << info.bufferShaderPaths.size();
            QVariantMap translatedParams = registry->translateParamsToUniforms(shaderId, rawParams);
            applyShaderInfoToWindow(slot, info, translatedParams, geometry, physScreenGeom);
        }
    }

    QMetaObject::Connection geomConn = installOverlayGeometryWatcher(physScreen, screenId, isVS);
    if (usingShader) {
        writeQmlProperty(slot, QStringLiteral("zoneDataVersion"), m_zoneDataVersion);
    }
    state->overlayGeomConnection = geomConn;
}

void OverlayService::refreshOverlayPropertiesIfShown()
{
    // Only the live overlay needs this: when hidden, the next show() re-resolves
    // the override through initializeOverlay (destroyIfTypeMismatch +
    // updateOverlayWindow), so a hidden overlay already picks up the rule change.
    if (!isOverlayDisplaying()) {
        return;
    }
    // A style override can flip whether a screen uses the shader overlay (the
    // Loader's `useShader`); recreate the mismatched slots first (no-op when no
    // type changed), then re-push each window's effective shader id/params via
    // updateGeometries() → updateOverlayWindow().
    recreateOverlayWindowsOnTypeMismatch();
    updateGeometries();
}

void OverlayService::recreateOverlayWindowsOnTypeMismatch()
{
    // Post-shell-migration the per-screen overlay slot hosts both modes
    // via Loader.sourceComponent switching on the slot's `useShader`
    // property - there's no surface to recreate. Walk every screen, flip
    // useShader if it diverged from current settings, and reload the
    // slot's content (toggle `loaded` false→true) so the Loader rebuilds
    // with the now-correct sourceComponent.
    QStringList screensToFlip;
    for (auto it = m_screenStates.constBegin(); it != m_screenStates.constEnd(); ++it) {
        auto* slot = it.value().mainOverlaySlot();
        if (!slot)
            continue;
        const bool slotIsShader = slot->property("useShader").toBool();
        const bool shouldUseShader = useShaderForScreen(it.key());
        if (slotIsShader != shouldUseShader)
            screensToFlip.append(it.key());
    }
    if (screensToFlip.isEmpty())
        return;

    const bool wasVisible = m_visible;
    if (wasVisible)
        stopShaderAnimation();

    for (const QString& screenId : screensToFlip) {
        if (isSnappingContextInactive(screenId)) {
            continue;
        }
        QScreen* physScreen = m_screenStates.value(screenId).overlayPhysScreen;
        if (!physScreen)
            continue;
        const QRect geom = m_screenStates.value(screenId).overlayGeometry;
        // createOverlayWindow now drives the slot - flips useShader,
        // toggles loaded, applies shader info - without recreating the
        // wl_surface. If the slot was visible before, it stays visible.
        createOverlayWindow(screenId, physScreen, geom.isValid() ? geom : physScreen->geometry());
        updateOverlayWindow(screenId, physScreen);
    }
    // Gate the render-loop restart + zone repopulation on isOverlayDisplaying(),
    // not wasVisible: while warm-idled (m_visible but m_overlayIdled — windows
    // kept alive after a drag), a settings toggle or live-edit reaches here, and
    // restarting the 60 Hz loop + re-pushing zones would un-blank the overlay and
    // undo the idle quiesce. The next refreshFromIdle() on the following drag
    // repopulates and restarts. Mirrors the same gate in updateLayout().
    if (isOverlayDisplaying() && anyScreenUsesShader()) {
        updateZonesForAllWindows();
        startShaderAnimation();
    }
}

void OverlayService::dismissOverlayWindow(QScreen* screen)
{
    const QString physId = PhosphorScreens::ScreenIdentity::identifierFor(screen);

    // Collect matching overlay keys - may be virtual screen IDs for this physical screen
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
        qCDebug(lcOverlay) << "dismissOverlayWindow: no state for" << screenId;
        return;
    }
    auto* slot = it->mainOverlaySlot();
    if (!slot) {
        qCDebug(lcOverlay) << "dismissOverlayWindow: no slot for" << screenId << "(shell creation may have failed)";
        // Clean up partial state even when slot is null - the shell
        // surface may never have been created (transport unavailable),
        // but we still need to clear the sentinels so a later recreate
        // doesn't think a stale entry is live.
        QObject::disconnect(it->overlayGeomConnection);
        it->overlayGeomConnection = {};
        it->overlayPhysScreen = nullptr;
        it->overlayGeometry = QRect();
        it->labelsTextureHash = 0;
        return;
    }
    // Post-shell-migration: there's no separate wl_surface to destroy.
    // Cancel any in-flight (surface, slot) animator op and run the
    // configured hide leg cleanly via beginHide rather than yanking
    // setVisible(false) out from under a possibly-still-running
    // beginShow - that would leave the Track in m_pendingDestroy with
    // a stale onComplete callback racing the next show.
    auto* shellSurface = it->shell ? it->shell->shellSurface() : nullptr;
    if (shellSurface) {
        m_shellHost->hideSlot(screenId, PhosphorSlotKeys::MainOverlay(), [this, screenIdCopy = screenId]() {
            auto sit = m_screenStates.find(screenIdCopy);
            if (sit == m_screenStates.end() || !sit->mainOverlaySlot()) {
                return;
            }
            QObject::disconnect(sit->overlayGeomConnection);
            sit->overlayGeomConnection = {};
            sit->overlayPhysScreen = nullptr;
            sit->overlayGeometry = QRect();
            // Release the labels payload (the sparse glyph-tile ZoneLabelTexture)
            // the persistent slot property would otherwise keep resident for the
            // whole session while hidden (the wallpaper reset is symmetric only -
            // its image is pinned by ShaderRegistry's static cache; see
            // releaseOverlaySlotTextures). Zeroing labelsTextureHash forces
            // updateLabelsTextureForWindow to rebuild on the next show()
            // instead of short-circuiting on the now-1x1 placeholder (which
            // would render no labels). The trade is one ZoneLabelTextureBuilder
            // rebuild per drag-start vs. the sparse glyph-tile payload held idle
            // per screen; the warm drag-pause path (setIdleForDragPause) is
            // untouched and keeps the texture warm mid-drag.
            releaseOverlaySlotTextures(sit->mainOverlaySlot());
            sit->labelsTextureHash = 0;
            writeQmlProperty(sit->mainOverlaySlot(), QStringLiteral("loaded"), false);
            sit->mainOverlaySlot()->setVisible(false);
            syncPassiveShellSurfaceState(screenIdCopy);
        });
    } else {
        // No shell surface - fall back to the immediate-toggle path so
        // the slot at least lands in the right state. Should not
        // normally happen post-ensurePassiveShellFor.
        QObject::disconnect(it->overlayGeomConnection);
        it->overlayGeomConnection = {};
        it->overlayPhysScreen = nullptr;
        it->overlayGeometry = QRect();
        // Mirror the shell-surface path: release the slot's labels + wallpaper
        // textures and reset the hash so the next show() rebuilds correctly.
        releaseOverlaySlotTextures(slot);
        it->labelsTextureHash = 0;
        writeQmlProperty(slot, QStringLiteral("loaded"), false);
        slot->setVisible(false);
        syncPassiveShellSurfaceState(screenId);
    }
}

void OverlayService::destroyOverlayWindow(QScreen* screen)
{
    const QString screenId = PhosphorScreens::ScreenIdentity::identifierFor(screen);
    qCDebug(lcOverlay) << "destroyOverlayWindow:" << screenId;
    destroyOverlayWindow(screenId);
}

void OverlayService::destroyOverlayWindow(const QString& screenId)
{
    qCDebug(lcOverlay) << "destroyOverlayWindow:" << screenId;
    auto it = m_screenStates.find(screenId);
    if (it == m_screenStates.end()) {
        return;
    }
    QObject::disconnect(it->overlayGeomConnection);
    // Reset the main-overlay context sentinel + cached geom for this
    // screen. The shell wl_surface stays alive (via shell->shellSurface())
    // until destroyPassiveShell runs, hosting any other slots
    // (OSD / snap-assist / picker / zone-selector) independently of
    // the main overlay's lifetime.
    it->overlayPhysScreen = nullptr;
    it->overlayGeometry = QRect();
    it->overlayGeomConnection = {};
    // Release the slot's labels payload too. A shader->non-shader
    // type flip routes through here (destroyIfTypeMismatch) and the
    // non-shader createOverlayWindow reload does NOT overwrite labelsTexture,
    // so without this the slot would pin the last shader-mode labels payload for
    // the screen's whole non-shader session. The screen-teardown callers
    // immediately destroyPassiveShell, where this is a harmless no-op on an
    // about-to-be-freed slot. Mirrors dismissOverlayWindow's release.
    releaseOverlaySlotTextures(it->mainOverlaySlot());
    it->labelsTextureHash = 0;
}

void OverlayService::updateOverlayWindow(QScreen* screen)
{
    const QString screenId = PhosphorScreens::ScreenIdentity::identifierFor(screen);
    updateOverlayWindow(screenId, screen);
}

void OverlayService::updateOverlayWindow(const QString& screenId, QScreen* physScreen)
{
    auto* slot = m_screenStates.value(screenId).mainOverlaySlot();
    if (!slot) {
        return;
    }

    PhosphorZones::Layout* screenLayout = resolveScreenLayout(screenId);

    // Per-context overlay overrides (shader / style + appearance) resolved once
    // for this screen's live context; layered over config below and reused by
    // the shader block further down.
    const PhosphorZones::ContextOverlayOverride overlayOverride = overlayOverrideForScreen(m_layoutManager, screenId);

    if (m_settings) {
        writeColorSettings(slot, m_settings, &overlayOverride);
        writeQmlProperty(slot, QStringLiteral("borderWidth"),
                         overlayOverride.borderWidth.value_or(m_settings->borderWidth()));
        writeQmlProperty(slot, QStringLiteral("borderRadius"),
                         overlayOverride.borderRadius.value_or(m_settings->borderRadius()));
        // The rule overrides the global show-numbers setting; the per-layout
        // gate still wins (a layout that hides numbers keeps them hidden).
        bool showNumbers = overlayOverride.showZoneNumbers.value_or(m_settings->showZoneNumbers())
            && (!screenLayout || screenLayout->showZoneNumbers());
        writeQmlProperty(slot, QStringLiteral("showNumbers"), showNumbers);
        writeFontProperties(slot, m_settings, /*includeLabelFontColor=*/true);
    }

    const bool windowIsShader = slot->property("useShader").toBool();
    const bool screenUsesShader = useShaderForScreen(screenId);
    if (windowIsShader && screenUsesShader && screenLayout) {
        auto* registry = m_shaderRegistry;
        if (registry) {
            const QString shaderId = overlayOverride.shaderId.value_or(screenLayout->shaderId());
            const QVariantMap rawParams =
                overlayOverride.shaderId ? overlayOverride.shaderParams : screenLayout->shaderParams();
            const ShaderRegistry::ShaderInfo info = registry->shader(shaderId);
            QVariantMap translatedParams = registry->translateParamsToUniforms(shaderId, rawParams);
            const QRect vsGeom = resolveScreenGeometry(m_screenManager, screenId);
            const QRect physGeom = physScreen ? physScreen->geometry() : vsGeom;
            applyShaderInfoToWindow(slot, info, translatedParams, vsGeom, physGeom);
        }
    } else if (windowIsShader && !screenUsesShader) {
        writeQmlProperty(slot, QStringLiteral("shaderSource"), QUrl());
        writeQmlProperty(slot, QStringLiteral("bufferShaderPath"), QString());
        writeQmlProperty(slot, QStringLiteral("bufferShaderPaths"), QVariant::fromValue(QStringList()));
        writeQmlProperty(slot, QStringLiteral("bufferFeedback"), false);
        writeQmlProperty(slot, QStringLiteral("bufferScale"), 1.0);
        writeQmlProperty(slot, QStringLiteral("bufferWrap"), QStringLiteral("clamp"));
        writeQmlProperty(slot, QStringLiteral("bufferWraps"), QStringList());
        writeQmlProperty(slot, QStringLiteral("bufferFilter"), QStringLiteral("linear"));
        writeQmlProperty(slot, QStringLiteral("bufferFilters"), QStringList());
        writeQmlProperty(slot, QStringLiteral("useDepthBuffer"), false);
        writeQmlProperty(slot, QStringLiteral("shaderParams"), QVariantMap());
    }

    QVariantList zones = buildZonesList(screenId, physScreen);
    QVariantList patched = patchZonesWithHighlight(zones, slot);

    // Pass previewZones (all zones with relative geometries) only when LayoutPreview mode is active
    bool anyZoneUsesPreview = false;
    for (const QVariant& z : std::as_const(patched)) {
        if (z.toMap().value(::PhosphorZones::ZoneJsonKeys::OverlayDisplayMode).toInt() == 1) {
            anyZoneUsesPreview = true;
            break;
        }
    }
    writeQmlProperty(slot, QStringLiteral("previewZones"), anyZoneUsesPreview ? patched : QVariantList{});
    writeQmlProperty(slot, QStringLiteral("zones"), patched);

    if (windowIsShader && screenUsesShader) {
        int highlightedCount = 0;
        for (const QVariant& z : patched) {
            if (z.toMap().value(QLatin1String("isHighlighted")).toBool()) {
                ++highlightedCount;
            }
        }
        writeQmlProperty(slot, QStringLiteral("zoneCount"), patched.size());
        writeQmlProperty(slot, QStringLiteral("highlightedCount"), highlightedCount);
        updateLabelsTextureForWindow(slot, patched, physScreen, screenLayout);
        // Note: zoneDataVersion is bumped and broadcast to all windows in
        // updateZonesForAllWindows() after all per-screen updates complete. Do not
        // write it here - updateOverlayWindow() is called per-screen, and
        // writing the version mid-loop would cause inconsistent state across
        // windows (some see the new version, others the old one).
    }
}

} // namespace PlasmaZones
