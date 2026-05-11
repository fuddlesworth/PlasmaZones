// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "internal.h"
#include "../overlayservice.h"
#include "../../core/logging.h"
#include "pz_slot_keys.h"
#include <PhosphorOverlay/ShellHost.h>
#include <PhosphorSurfaces/SurfaceManager.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutUtils.h>
#include "../../core/utils.h"
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/VirtualScreen.h>
#include "../snapassistthumbnailprovider.h"
#include <QGuiApplication>
#include <QImage>
#include <QQuickWindow>
#include <QScreen>
#include <QQmlEngine>
#include <QKeyEvent>
#include <QTimer>
#include <QUrl>

#include <PhosphorLayer/Surface.h>
#include <PhosphorLayer/ILayerShellTransport.h>
#include "pz_roles.h"
#include <PhosphorScreens/ScreenIdentity.h>

namespace PlasmaZones {

namespace {

/// Convert EmptyZoneList to QVariantList for QML property push
QVariantList emptyZonesToVariantList(const EmptyZoneList& zones)
{
    QVariantList result;
    result.reserve(zones.size());
    for (const auto& z : zones) {
        QVariantMap m;
        m[QStringLiteral("zoneId")] = z.zoneId;
        m[QStringLiteral("x")] = z.x;
        m[QStringLiteral("y")] = z.y;
        m[QStringLiteral("width")] = z.width;
        m[QStringLiteral("height")] = z.height;
        m[QStringLiteral("borderWidth")] = z.borderWidth;
        m[QStringLiteral("borderRadius")] = z.borderRadius;
        m[QStringLiteral("useCustomColors")] = z.useCustomColors;
        if (z.useCustomColors) {
            m[QStringLiteral("highlightColor")] = z.highlightColor;
            m[QStringLiteral("inactiveColor")] = z.inactiveColor;
            m[QStringLiteral("borderColor")] = z.borderColor;
            m[QStringLiteral("activeOpacity")] = z.activeOpacity;
            m[QStringLiteral("inactiveOpacity")] = z.inactiveOpacity;
        }
        result.append(m);
    }
    return result;
}

/// Convert SnapAssistCandidateList to QVariantList for QML property push
QVariantList candidatesToVariantList(const SnapAssistCandidateList& candidates)
{
    QVariantList result;
    result.reserve(candidates.size());
    for (const auto& c : candidates) {
        QVariantMap m;
        m[QStringLiteral("windowId")] = c.windowId;
        m[QStringLiteral("compositorHandle")] = c.compositorHandle;
        m[QStringLiteral("icon")] = c.icon;
        m[QStringLiteral("caption")] = c.caption;
        result.append(m);
    }
    return result;
}

} // namespace

void OverlayService::showSnapAssist(const QString& screenId, const EmptyZoneList& emptyZones,
                                    const SnapAssistCandidateList& candidates)
{
    if (emptyZones.isEmpty() || candidates.isEmpty()) {
        qCDebug(lcOverlay) << "showSnapAssist: no empty zones or candidates";
        Q_EMIT snapAssistDismissed();
        return;
    }

    QScreen* screen = resolveTargetScreen(m_screenManager, screenId);
    if (!screen) {
        qCWarning(lcOverlay) << "showSnapAssist: no screen available";
        Q_EMIT snapAssistDismissed();
        return;
    }

    const QVariantList zonesList = emptyZonesToVariantList(emptyZones);
    QVariantList candidatesList = candidatesToVariantList(candidates);

    // Stale-request guard: KWin effect computes empty zones async; layout
    // may have switched in between. Verify at least one zone still exists.
    PhosphorZones::Layout* currentLayout = resolveScreenLayout(screenId);
    if (currentLayout) {
        bool anyValid = false;
        for (const auto& z : emptyZones) {
            if (!z.zoneId.isEmpty() && currentLayout->zoneById(QUuid::fromString(z.zoneId))) {
                anyValid = true;
                break;
            }
        }
        if (!anyValid) {
            qCInfo(lcOverlay) << "showSnapAssist: stale request — zone IDs do not match current layout"
                              << currentLayout->name();
            Q_EMIT snapAssistDismissed();
            return;
        }
    }

    QRect screenGeom = resolveScreenGeometry(m_screenManager, screenId);
    if (!screenGeom.isValid()) {
        screenGeom = screen->geometry();
    }

    // Resolve target shell — per-screen shell hosts the snap-assist slot.
    auto* state = ensurePassiveShellFor(screenId, screen);
    if (!state || !state->shell || !state->shell->shellSurface() || !state->snapAssistSlot()) {
        qCWarning(lcOverlay) << "showSnapAssist: no passive shell for screen=" << screenId;
        Q_EMIT snapAssistDismissed();
        return;
    }

    // If snap-assist is currently shown on a DIFFERENT screen, dismiss
    // it there first — snap-assist is a singleton across all screens.
    // Animator-driven beginHide on (prevSurface, prevSlot,
    // PzRoles::SnapAssist) keys ONLY the snap-assist track via the
    // per-(Surface, target) animator keying — sibling slots on the
    // same shell (OSD, zone-selector) keep animating cleanly.
    if (m_snapAssistVisible && !m_snapAssistScreenId.isEmpty() && m_snapAssistScreenId != screenId) {
        const QString prevScreenId = m_snapAssistScreenId;
        auto prevIt = m_screenStates.find(prevScreenId);
        if (prevIt != m_screenStates.end() && prevIt->shell && prevIt->shell->shellSurface()
            && prevIt->snapAssistSlot()) {
            m_shellHost->hideSlot(prevScreenId, PzSlotKeys::SnapAssist(), [this, prevScreenId]() {
                onSnapAssistSlotHideCompleted(prevScreenId);
            });
        }
    }

    m_snapAssistScreenId = screenId;
    m_snapAssistVisible = true;

    // Hide the zone selector for the specific virtual screen where snap
    // assist is showing — selectors on adjacent VS of the same physical
    // monitor stay visible. Reset cursor state first so a re-show after
    // dismiss doesn't tick edge-scroll on stale drag-time cursor coords.
    hideZoneSelectorSlotOnScreen(screenId);

    // Attach cached thumbnails — kwin-effect posts updates via
    // setSnapAssistThumbnail asynchronously after this returns.
    QVariantList rebuilt;
    rebuilt.reserve(candidatesList.size());
    int cachedCount = 0;
    for (int i = 0; i < candidatesList.size(); ++i) {
        QVariantMap cand = candidatesList[i].toMap();
        const QString compositorHandle = cand.value(QStringLiteral("compositorHandle")).toString();
        auto* thumbProvider = m_thumbnailProvider.load(std::memory_order_acquire);
        if (!compositorHandle.isEmpty() && thumbProvider) {
            const QString cachedUrl = thumbProvider->urlFor(compositorHandle);
            if (!cachedUrl.isEmpty()) {
                cand[QStringLiteral("thumbnail")] = cachedUrl;
                ++cachedCount;
            }
        }
        rebuilt.append(cand);
    }
    m_snapAssistCandidates = std::move(rebuilt);
    qCDebug(lcOverlay) << "showSnapAssist:" << cachedCount << "cached thumbnails;"
                       << "remaining will arrive from kwin-effect via setSnapAssistThumbnail";

    auto* slot = state->snapAssistSlot();
    auto* shellSurface = state->shell->shellSurface();
    auto* shellWindow = state->shell->shellWindow();

    writeQmlProperty(slot, QStringLiteral("emptyZones"), zonesList);
    writeQmlProperty(slot, QStringLiteral("candidates"), m_snapAssistCandidates);
    writeQmlProperty(slot, QStringLiteral("screenWidth"), screenGeom.width());
    writeQmlProperty(slot, QStringLiteral("screenHeight"), screenGeom.height());

    writeColorSettings(slot, m_settings);
    if (m_settings) {
        writeQmlProperty(slot, QStringLiteral("borderWidth"), m_settings->borderWidth());
        writeQmlProperty(slot, QStringLiteral("borderRadius"), m_settings->borderRadius());
    }

    // Resize the shell window to the target screen geometry (matches
    // OSD path's sizeOsdToScreen). The shell is shared with OSD,
    // selector, and picker slots: sizing it to anything other than the
    // VS rect would shift their positioning to physical-screen
    // coordinates, so the snap-assist path holds to the same per-VS
    // sizing the rest of the shell uses.
    if (shellWindow) {
        assertWindowOnScreen(shellWindow, screen, screenGeom);
        shellWindow->setWidth(screenGeom.width());
        shellWindow->setHeight(screenGeom.height());
    }

    // OSD-style content lifecycle: toggle `loaded` false→true so the
    // Loader re-instantiates SnapAssistContent with a fresh shaderAnchor
    // QQuickItem each show. Avoids stale FBO content on subsequent
    // vertex-shader transitions.
    writeQmlProperty(slot, QStringLiteral("loaded"), false);
    writeQmlProperty(slot, QStringLiteral("loaded"), true);

    cancelSurfacePrime(shellSurface);
    if (!shellSurface->isLogicallyShown()) {
        shellSurface->show();
    }
    slot->setVisible(true);
    m_surfaceAnimator->beginShow(shellSurface, slot, PzRoles::SnapAssist, []() { });
    // Snap-assist is modal — needs input for click-to-select. The
    // sync re-evaluates the input region now that the modal slot is
    // visible so `Qt::WindowTransparentForInput` flips off.
    syncPassiveShellSurfaceStateForSurface(shellSurface);

    qCInfo(lcOverlay) << "showSnapAssist: screen=" << screenId << "zones=" << emptyZones.size()
                      << "candidates=" << candidates.size();

    // snapAssistShown signal is wired in start.cpp to
    // ensureCancelOverlayShortcutRegistered() — the shell's wl_surface is
    // kbd-None so the per-content QML Shortcut path used by the legacy
    // SnapAssistOverlay can't fire here. KGlobalAccel grab of Escape +
    // cancelSnap()'s existing isSnapAssistVisible() branch (see
    // windowdragadaptor.cpp:265) routes Escape to hideSnapAssist().
    Q_EMIT snapAssistShown(screenId, emptyZones, candidates);
}

bool OverlayService::setSnapAssistThumbnail(const QString& compositorHandle, int width, int height,
                                            const QByteArray& pixels)
{
    // External entry point. The kwin-effect renders a candidate through
    // KWin's OffscreenQuickScene + WindowThumbnail and posts the result as
    // raw ARGB32 (non-premultiplied) pixels — no PNG encode, no base64.
    // OverlayAdaptor::setSnapAssistThumbnail authenticates the sender and
    // applies a coarse byte-cap before this slot runs; we still validate
    // shape here because IOverlayService is a public C++ API and direct
    // (non-D-Bus) callers may exist.
    //
    // Return value drives the caller's recently-posted dedup set. A false
    // return MUST keep the handle out of the dedup window so the next
    // snap-assist for that window re-captures instead of stranding on the
    // icon fallback.
    //
    // Bounds: a 256² thumbnail is the steady-state size; 1024² is the
    // ceiling. Anything larger is almost certainly a marshalling bug or a
    // hostile sender that slipped past auth, and consumes excessive bytes
    // to round-trip through the cache.
    static constexpr int MaxDimension = 1024;
    if (width <= 0 || height <= 0 || width > MaxDimension || height > MaxDimension) {
        qCDebug(lcOverlay) << "setSnapAssistThumbnail: invalid dimensions" << width << "x" << height << "for"
                           << compositorHandle;
        return false;
    }
    const qsizetype expectedBytes = qsizetype(width) * qsizetype(height) * 4;
    if (pixels.size() != expectedBytes) {
        qCDebug(lcOverlay) << "setSnapAssistThumbnail: pixel-buffer size mismatch — got" << pixels.size() << "expected"
                           << expectedBytes << "for" << compositorHandle;
        return false;
    }
    // QImage(uchar*, ...) constructs a non-owning view over the byte buffer;
    // calling .copy() detaches into an owned image so the cache entry
    // survives @p pixels going out of scope. With dimensions and byte count
    // already validated above the constructor cannot produce a null image,
    // so no isNull guard is needed before .copy().
    QImage view(reinterpret_cast<const uchar*>(pixels.constData()), width, height, width * 4, QImage::Format_ARGB32);
    QImage image = view.copy();
    return updateSnapAssistCandidateThumbnail(compositorHandle, std::move(image));
}

bool OverlayService::updateSnapAssistCandidateThumbnail(const QString& compositorHandle, QImage image)
{
    auto* thumbProvider = m_thumbnailProvider.load(std::memory_order_acquire);
    if (image.isNull() || !thumbProvider) {
        return false;
    }
    const QString providerUrl = thumbProvider->insert(compositorHandle, std::move(image));
    if (providerUrl.isEmpty()) {
        return false;
    }
    // Cache insert succeeded — handle is held even if snap-assist isn't
    // currently open. Provider URL stays valid until LRU eviction.
    if (!m_snapAssistVisible || m_snapAssistScreenId.isEmpty()) {
        return true;
    }
    auto* slot = m_screenStates.value(m_snapAssistScreenId).snapAssistSlot();
    if (!slot) {
        return true;
    }
    for (int i = 0; i < m_snapAssistCandidates.size(); ++i) {
        QVariantMap cand = m_snapAssistCandidates[i].toMap();
        if (cand.value(QStringLiteral("compositorHandle")).toString() == compositorHandle) {
            cand[QStringLiteral("thumbnail")] = providerUrl;
            m_snapAssistCandidates[i] = cand;
            writeQmlProperty(slot, QStringLiteral("candidates"), m_snapAssistCandidates);
            qCDebug(lcOverlay) << "SnapAssist: thumbnail updated for" << compositorHandle;
            break;
        }
    }
    return true;
}

void OverlayService::hideSnapAssist()
{
    if (!m_snapAssistVisible || m_snapAssistScreenId.isEmpty()) {
        return;
    }

    const QString screenId = m_snapAssistScreenId;
    m_snapAssistCandidates.clear();
    m_snapAssistVisible = false;
    m_snapAssistScreenId.clear();

    auto stateIt = m_screenStates.find(screenId);
    if (stateIt != m_screenStates.end() && stateIt->shell && stateIt->shell->shellSurface()
        && stateIt->snapAssistSlot()) {
        m_shellHost->hideSlot(screenId, PzSlotKeys::SnapAssist(), [this, effectiveId = screenId]() {
            onSnapAssistSlotHideCompleted(effectiveId);
        });
    }

    // snapAssistDismissed → WindowDragAdaptor::onSnapAssistDismissed →
    // unregisterCancelOverlayShortcut() (windowdragadaptor.cpp:82).
    Q_EMIT snapAssistDismissed();

    // Zone-selector restore is owned by onSnapAssistSlotHideCompleted —
    // it fires when the snap-assist slot has finished its hide
    // animation, so the selector fades back in cleanly after the
    // snap-assist has visually cleared. A synchronous restore here
    // would race the in-flight beginHide and rely on the
    // showZoneSelectorSlotOnScreen short-circuit (which checks
    // slot->isVisible() — true mid-animation), risking visible
    // overlap or missed re-shows.
}

bool OverlayService::isSnapAssistVisible() const
{
    return m_snapAssistVisible;
}

void OverlayService::onSnapAssistDismissRequested()
{
    // Fired by SnapAssistContent's backdrop click → forwarded by the shell
    // window's `snapAssistDismissRequested` signal. Just route to the
    // standard hide path.
    hideSnapAssist();
}

void OverlayService::onSnapAssistSlotHideCompleted(const QString& effectiveId)
{
    auto it = m_screenStates.find(effectiveId);
    if (it == m_screenStates.end() || !it->snapAssistSlot()) {
        return;
    }
    it->snapAssistSlot()->setVisible(false);
    writeQmlProperty(it->snapAssistSlot(), QStringLiteral("loaded"), false);
    // Symmetric restore: showSnapAssist hid the zone-selector slot on
    // this screen via hideZoneSelectorSlotOnScreen. Owns BOTH the
    // user-dismiss path (hideSnapAssist routes here) and the
    // cross-screen singleton-dismiss path in showSnapAssist's
    // prev-screen branch. Restore the selector if the drag is
    // still logically active.
    if (m_zoneSelectorVisible && it->zoneSelectorPhysScreen && it->zoneSelectorGeometry.isValid()) {
        showZoneSelectorSlotOnScreen(effectiveId, it->zoneSelectorPhysScreen, it->zoneSelectorGeometry);
    }
    syncPassiveShellSurfaceState(effectiveId);
}

void OverlayService::onSnapAssistWindowSelected(const QString& windowId, const QString& zoneId,
                                                const QString& geometryJson)
{
    // Resolve screen id from the active snap-assist screen.
    Q_EMIT snapAssistWindowSelected(windowId, zoneId, geometryJson, m_snapAssistScreenId);
}

// ═══════════════════════════════════════════════════════════════════════════════
// PhosphorZones::Layout Picker Overlay
// ═══════════════════════════════════════════════════════════════════════════════

void OverlayService::showLayoutPicker(const QString& screenId)
{
    if (m_layoutPickerVisible) {
        return;
    }

    QScreen* screen = resolveTargetScreen(m_screenManager, screenId);
    if (!screen) {
        qCWarning(lcOverlay) << "showLayoutPicker: no screen available";
        return;
    }

    const QString resolvedId = screenId.isEmpty() ? Phosphor::Screens::ScreenIdentity::identifierFor(screen) : screenId;
    QRect screenGeom = resolveScreenGeometry(m_screenManager, resolvedId);
    if (!screenGeom.isValid()) {
        screenGeom = screen->geometry();
    }

    auto* state = ensurePassiveShellFor(resolvedId, screen);
    if (!state || !state->shell || !state->shell->shellSurface() || !state->layoutPickerSlot()) {
        qCWarning(lcOverlay) << "showLayoutPicker: no passive shell for screen=" << resolvedId;
        return;
    }

    // Hide the zone selector on this VS to avoid overlap.
    hideZoneSelectorSlotOnScreen(resolvedId);

    QSize autotileCanvas;
    if (m_screenManager) {
        const QRect avail = m_screenManager->screenAvailableGeometry(resolvedId);
        if (avail.isValid() && avail.width() > 0 && avail.height() > 0) {
            autotileCanvas = avail.size();
        }
    }
    QVariantList layoutsList = buildLayoutsList(resolvedId, autotileCanvas);
    if (layoutsList.isEmpty()) {
        qCDebug(lcOverlay) << "showLayoutPicker: no layouts available";
        return;
    }

    QString activeId;
    if (m_layoutManager) {
        PhosphorZones::Layout* activeLayout = resolveScreenLayout(resolvedId);
        if (activeLayout) {
            activeId = activeLayout->id().toString();
        }
    }

    qreal aspectRatio =
        (screenGeom.height() > 0) ? static_cast<qreal>(screenGeom.width()) / screenGeom.height() : (16.0 / 9.0);
    aspectRatio = qBound(0.5, aspectRatio, 4.0);

    auto* slot = state->layoutPickerSlot();
    auto* shellSurface = state->shell->shellSurface();
    auto* shellWindow = state->shell->shellWindow();

    writeQmlProperty(slot, QStringLiteral("layouts"), layoutsList);
    writeQmlProperty(slot, QStringLiteral("activeLayoutId"), activeId);
    writeQmlProperty(slot, QStringLiteral("screenAspectRatio"), aspectRatio);
    writeQmlProperty(slot, QStringLiteral("globalAutoAssign"), m_settings && m_settings->autoAssignAllLayouts());
    writeFontProperties(slot, m_settings);

    bool locked = false;
    if (m_settings && m_layoutManager) {
        int curDesktop = m_layoutManager->currentVirtualDesktop();
        QString curActivity = m_layoutManager->currentActivity();
        locked = isAnyModeLocked(m_settings, resolvedId, curDesktop, curActivity);
    }
    writeQmlProperty(slot, QStringLiteral("locked"), locked);
    writeColorSettings(slot, m_settings);

    if (shellWindow) {
        assertWindowOnScreen(shellWindow, screen, screenGeom);
        shellWindow->setWidth(screenGeom.width());
        shellWindow->setHeight(screenGeom.height());
    }

    // OSD-style content lifecycle: toggle false→true so the picker
    // Loader re-instantiates LayoutPickerContent fresh per show.
    writeQmlProperty(slot, QStringLiteral("loaded"), false);
    writeQmlProperty(slot, QStringLiteral("loaded"), true);

    cancelSurfacePrime(shellSurface);
    if (!shellSurface->isLogicallyShown()) {
        shellSurface->show();
    }
    slot->setVisible(true);
    m_surfaceAnimator->beginShow(shellSurface, slot, PzRoles::LayoutPicker, []() { });
    // Layout picker is modal — needs input for click-to-select.
    syncPassiveShellSurfaceStateForSurface(shellSurface);

    m_layoutPickerScreenId = resolvedId;
    m_layoutPickerVisible = true;

    qCInfo(lcOverlay) << "showLayoutPicker: screen=" << resolvedId << "layouts=" << layoutsList.size()
                      << "active=" << activeId;
}

void OverlayService::hideLayoutPicker()
{
    if (!m_layoutPickerVisible) {
        // Always emit dismissed so the daemon's Escape-shortcut release
        // path runs even on idempotent calls (defensive — multiple call
        // sites converge on hideLayoutPicker).
        Q_EMIT layoutPickerDismissed();
        return;
    }

    const QString screenId = m_layoutPickerScreenId;
    m_layoutPickerVisible = false;
    m_layoutPickerScreenId.clear();

    auto stateIt = m_screenStates.find(screenId);
    if (stateIt != m_screenStates.end() && stateIt->shell && stateIt->shell->shellSurface()
        && stateIt->layoutPickerSlot()) {
        m_shellHost->hideSlot(screenId, PzSlotKeys::LayoutPicker(), [this, effectiveId = screenId]() {
            onLayoutPickerSlotHideCompleted(effectiveId);
        });
    }

    // Zone-selector restore is owned by onLayoutPickerSlotHideCompleted —
    // mirror of the snap-assist + OSD ownership pattern. The synchronous
    // fast-path raced the in-flight beginHide.

    Q_EMIT layoutPickerDismissed();
}

bool OverlayService::isLayoutPickerVisible() const
{
    return m_layoutPickerVisible;
}

void OverlayService::onLayoutPickerSlotHideCompleted(const QString& effectiveId)
{
    auto it = m_screenStates.find(effectiveId);
    if (it == m_screenStates.end() || !it->layoutPickerSlot()) {
        return;
    }
    it->layoutPickerSlot()->setVisible(false);
    writeQmlProperty(it->layoutPickerSlot(), QStringLiteral("loaded"), false);
    // Symmetric restore — see onSnapAssistSlotHideCompleted /
    // onOsdSlotHideCompleted. The picker hid the zone-selector slot
    // on show; restore it once the picker has finished its hide
    // animation (drag may still be active).
    if (m_zoneSelectorVisible && it->zoneSelectorPhysScreen && it->zoneSelectorGeometry.isValid()) {
        showZoneSelectorSlotOnScreen(effectiveId, it->zoneSelectorPhysScreen, it->zoneSelectorGeometry);
    }
    syncPassiveShellSurfaceState(effectiveId);
}

void OverlayService::onLayoutPickerDismissRequested()
{
    // Backdrop click forwarded from the shell — same as Escape route.
    hideLayoutPicker();
}

void OverlayService::onLayoutPickerSelected(const QString& layoutId)
{
    qCInfo(lcOverlay) << "Layout picker selected=" << layoutId;
    hideLayoutPicker();
    Q_EMIT layoutPickerSelected(layoutId);
}

void OverlayService::pickerMoveSelection(int dx, int dy)
{
    if (!m_layoutPickerVisible || m_layoutPickerScreenId.isEmpty()) {
        return;
    }
    auto* slot = m_screenStates.value(m_layoutPickerScreenId).layoutPickerSlot();
    if (!slot) {
        return;
    }
    QMetaObject::invokeMethod(slot, "moveSelection", Q_ARG(QVariant, dx), Q_ARG(QVariant, dy));
}

void OverlayService::pickerConfirmSelection()
{
    if (!m_layoutPickerVisible || m_layoutPickerScreenId.isEmpty()) {
        return;
    }
    auto* slot = m_screenStates.value(m_layoutPickerScreenId).layoutPickerSlot();
    if (!slot) {
        return;
    }
    QMetaObject::invokeMethod(slot, "confirmSelection");
}

} // namespace PlasmaZones
