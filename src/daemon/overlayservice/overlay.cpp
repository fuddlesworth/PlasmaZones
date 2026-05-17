// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "internal.h"
#include "../overlayservice.h"
#include "qml_property_names.h"
#include "../../core/logging.h"
#include "pz_slot_keys.h"
#include <PhosphorOverlay/ShellHost.h>
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
#include <PhosphorAnimation/SurfaceAnimator.h>
#include "pz_roles.h"
#include <PhosphorScreens/ScreenIdentity.h>

namespace PlasmaZones {

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
        cursorEffectiveId = Phosphor::Screens::ScreenIdentity::identifierFor(cursorScreen);
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
            const Phosphor::Screens::PhysicalScreen phys = mgr->physicalScreenFor(screenId);
            QScreen* physScreen = phys.qscreen;
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
            // Post-shell-migration: shell window stays mapped permanently;
            // animation drives the per-content slot's opacity. Surface::show()
            // only fires on the very first transition Hidden→Shown.
            auto* shellSurface = shellState->shellSurface();
            auto* slot = m_screenStates[screenId].mainOverlaySlot();
            if (shellSurface && slot) {
                if (!shellSurface->isLogicallyShown()) {
                    shellSurface->show();
                }
                slot->setVisible(true);
                m_surfaceAnimator->beginShow(shellSurface, slot, PzRoles::ZoneOverlay, []() { });
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
    const QString screenId = Phosphor::Screens::ScreenIdentity::identifierFor(screen);
    auto* mgr = m_screenManager;
    QRect geom = (mgr && mgr->screenGeometry(screenId).isValid()) ? mgr->screenGeometry(screenId) : screen->geometry();
    createOverlayWindow(screenId, screen, geom);
}

void OverlayService::createOverlayWindow(const QString& screenId, QScreen* physScreen, const QRect& geometry)
{
    // Post-shell-migration: the per-screen PzRoles::ZoneOverlay wl_surface
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
            const QString shaderId = screenLayout->shaderId();
            const ShaderRegistry::ShaderInfo info = registry->shader(shaderId);
            qCDebug(lcOverlay) << "Overlay shader=" << shaderId << "multipass=" << info.isMultipass
                               << "bufferPaths=" << info.bufferShaderPaths.size();
            QVariantMap translatedParams = registry->translateParamsToUniforms(shaderId, screenLayout->shaderParams());
            applyShaderInfoToWindow(slot, info, translatedParams, geometry, physScreenGeom);
        }
    }

    QMetaObject::Connection geomConn = installOverlayGeometryWatcher(physScreen, screenId, isVS);
    if (usingShader) {
        writeQmlProperty(slot, QStringLiteral("zoneDataVersion"), m_zoneDataVersion);
    }
    state->overlayGeomConnection = geomConn;
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
        if (isContextDisabled(m_settings, PhosphorZones::AssignmentEntry::Snapping, screenId, m_currentVirtualDesktop,
                              m_currentActivity)) {
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
    if (wasVisible && anyScreenUsesShader()) {
        updateZonesForAllWindows();
        startShaderAnimation();
    }
}

void OverlayService::dismissOverlayWindow(QScreen* screen)
{
    const QString physId = Phosphor::Screens::ScreenIdentity::identifierFor(screen);

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
        m_shellHost->hideSlot(screenId, PzSlotKeys::MainOverlay(), [this, screenIdCopy = screenId]() {
            auto sit = m_screenStates.find(screenIdCopy);
            if (sit == m_screenStates.end() || !sit->mainOverlaySlot()) {
                return;
            }
            QObject::disconnect(sit->overlayGeomConnection);
            sit->overlayGeomConnection = {};
            sit->overlayPhysScreen = nullptr;
            sit->overlayGeometry = QRect();
            // labelsTextureHash is intentionally NOT cleared - the
            // QML labelsTexture property still holds the previously-
            // built image, and updateLabelsTextureForWindow's hash
            // compare on the next show() will detect any genuine
            // input change and rebuild only then. Zeroing the hash
            // would force a redundant 23 MB QImage rebuild on every
            // hide/show cycle even for unchanged zone inputs.
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
        writeQmlProperty(slot, QStringLiteral("loaded"), false);
        slot->setVisible(false);
        syncPassiveShellSurfaceState(screenId);
    }
}

void OverlayService::destroyOverlayWindow(QScreen* screen)
{
    const QString screenId = Phosphor::Screens::ScreenIdentity::identifierFor(screen);
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
    it->labelsTextureHash = 0;
}

void OverlayService::updateOverlayWindow(QScreen* screen)
{
    const QString screenId = Phosphor::Screens::ScreenIdentity::identifierFor(screen);
    updateOverlayWindow(screenId, screen);
}

void OverlayService::updateOverlayWindow(const QString& screenId, QScreen* physScreen)
{
    auto* slot = m_screenStates.value(screenId).mainOverlaySlot();
    if (!slot) {
        return;
    }

    PhosphorZones::Layout* screenLayout = resolveScreenLayout(screenId);

    if (m_settings) {
        writeColorSettings(slot, m_settings);
        writeQmlProperty(slot, QStringLiteral("borderWidth"), m_settings->borderWidth());
        writeQmlProperty(slot, QStringLiteral("borderRadius"), m_settings->borderRadius());
        writeQmlProperty(slot, QStringLiteral("enableBlur"), m_settings->enableBlur());
        bool showNumbers = m_settings->showZoneNumbers() && (!screenLayout || screenLayout->showZoneNumbers());
        writeQmlProperty(slot, QStringLiteral("showNumbers"), showNumbers);
        writeFontProperties(slot, m_settings);
    }

    const bool windowIsShader = slot->property("useShader").toBool();
    const bool screenUsesShader = useShaderForScreen(screenId);
    if (windowIsShader && screenUsesShader && screenLayout) {
        auto* registry = m_shaderRegistry;
        if (registry) {
            const QString shaderId = screenLayout->shaderId();
            const ShaderRegistry::ShaderInfo info = registry->shader(shaderId);
            QVariantMap translatedParams = registry->translateParamsToUniforms(shaderId, screenLayout->shaderParams());
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
        // updateGeometries() after all per-screen updates complete. Do not
        // write it here - updateOverlayWindow() is called per-screen, and
        // writing the version mid-loop would cause inconsistent state across
        // windows (some see the new version, others the old one).
    }
}

} // namespace PlasmaZones
