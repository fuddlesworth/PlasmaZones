// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "internal.h"
#include "../overlayservice.h"
#include "../../core/logging.h"
#include "phosphor_slot_keys.h"
#include <PhosphorOverlay/ShellHost.h>
#include <PhosphorSurfaces/SurfaceManager.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutUtils.h>
#include "../../core/utils.h"
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/VirtualScreen.h>
#include "../snapassistthumbnailprovider.h"
#include "../dmabuftextureprovider.h"
#include "../dmabuffencewaiter.h"
#include <QGuiApplication>
#include <QImage>
#include <QQuickWindow>
#include <QScreen>
#include <QQmlEngine>
#include <QKeyEvent>
#include <QTimer>

#include <unistd.h>
#include <QUrl>

#include <PhosphorLayer/Surface.h>
#include <PhosphorLayer/ILayerShellTransport.h>
#include "phosphor_roles.h"
#include <PhosphorScreens/ScreenIdentity.h>

namespace PlasmaZones {

namespace {

/// Grace period before a dismissed snap-assist's thumbnail cache is trimmed.
/// Long enough that any interactive continuation (dismiss → re-trigger,
/// multi-zone fill, cross-screen move) restarts the timer first and keeps the
/// warm cache; short enough that the bounded cache doesn't linger for the rest
/// of the session after the user is done snapping.
constexpr int SnapAssistCacheTrimDelayMs = 30000;

/// Convert PhosphorProtocol::EmptyZoneList to QVariantList for QML property push
QVariantList emptyZonesToVariantList(const PhosphorProtocol::EmptyZoneList& zones)
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

/// Convert PhosphorProtocol::SnapAssistCandidateList to QVariantList for QML property push
QVariantList candidatesToVariantList(const PhosphorProtocol::SnapAssistCandidateList& candidates)
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

void OverlayService::showSnapAssist(const QString& screenId, const PhosphorProtocol::EmptyZoneList& emptyZones,
                                    const PhosphorProtocol::SnapAssistCandidateList& candidates)
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
            qCInfo(lcOverlay) << "showSnapAssist: stale request - zone IDs do not match current layout"
                              << currentLayout->name();
            Q_EMIT snapAssistDismissed();
            return;
        }
    }

    QRect screenGeom = resolveScreenGeometry(m_screenManager, screenId);
    if (!screenGeom.isValid()) {
        screenGeom = screen->geometry();
    }

    // Resolve target shell - per-screen shell hosts the snap-assist slot.
    auto* state = ensurePassiveShellFor(screenId, screen);
    if (!state || !state->shell || !state->shell->shellSurface() || !state->snapAssistSlot()) {
        qCWarning(lcOverlay) << "showSnapAssist: no passive shell for screen=" << screenId;
        Q_EMIT snapAssistDismissed();
        return;
    }

    // If snap-assist is currently shown on a DIFFERENT screen, dismiss
    // it there first - snap-assist is a singleton across all screens.
    // Animator-driven beginHide on (prevSurface, prevSlot,
    // PhosphorRoles::SnapAssist) keys ONLY the snap-assist track via the
    // per-(Surface, target) animator keying - sibling slots on the
    // same shell (OSD, zone-selector) keep animating cleanly.
    if (m_snapAssistVisible && !m_snapAssistScreenId.isEmpty() && m_snapAssistScreenId != screenId) {
        const QString prevScreenId = m_snapAssistScreenId;
        auto prevIt = m_screenStates.find(prevScreenId);
        if (prevIt != m_screenStates.end() && prevIt->shell && prevIt->shell->shellSurface()
            && prevIt->snapAssistSlot()) {
            m_shellHost->hideSlot(prevScreenId, PhosphorSlotKeys::SnapAssist(), [this, prevScreenId]() {
                onSnapAssistSlotHideCompleted(prevScreenId);
            });
        }
    }

    m_snapAssistScreenId = screenId;
    m_snapAssistVisible = true;

    // Snap-assist is in active use again - cancel any pending cache trim so
    // the continuation lookups below (urlFor) hit the warm cache.
    if (m_snapAssistCacheTrimTimer) {
        m_snapAssistCacheTrimTimer->stop();
    }

    // Hide the zone selector for the specific virtual screen where snap
    // assist is showing - selectors on adjacent VS of the same physical
    // monitor stay visible. Reset cursor state first so a re-show after
    // dismiss doesn't tick edge-scroll on stale drag-time cursor coords.
    hideZoneSelectorSlotOnScreen(screenId);

    // Attach cached thumbnails - kwin-effect posts updates via
    // setSnapAssistThumbnail asynchronously after this returns.
    //
    // Hoist the atomic load out of the loop — it's stable across the
    // single-threaded GUI iteration here, so paying the per-iteration
    // acquire fence to re-read it was wasted work on a hot path that
    // can run with dozens of candidates during snap-assist setup.
    QVariantList rebuilt;
    rebuilt.reserve(candidatesList.size());
    int cachedCount = 0;
    auto* const thumbProvider = m_thumbnailProvider.load(std::memory_order_acquire);
    for (int i = 0; i < candidatesList.size(); ++i) {
        QVariantMap cand = candidatesList[i].toMap();
        const QString compositorHandle = cand.value(QStringLiteral("compositorHandle")).toString();
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

    const PhosphorZones::ContextOverlayOverride overlayOverride = overlayOverrideForScreen(m_layoutManager, screenId);
    writeColorSettings(slot, m_settings, &overlayOverride);
    if (m_settings) {
        writeQmlProperty(slot, QStringLiteral("borderWidth"),
                         overlayOverride.borderWidth.value_or(m_settings->borderWidth()));
        writeQmlProperty(slot, QStringLiteral("borderRadius"),
                         overlayOverride.borderRadius.value_or(m_settings->borderRadius()));
    }

    // Stage d: resolve + push the snap-assist surface-shader decoration (same
    // SurfaceDecoration host the OSD uses, retargeted to the "popup.snapAssist"
    // surface path). Empty source = no decoration (card draws natively).
    applyDecoration(slot, QStringLiteral("popup.snapAssist"));

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
    m_surfaceAnimator->beginShow(shellSurface, slot, PhosphorRoles::SnapAssist, []() { });
    // Snap-assist is modal - needs input for click-to-select. The
    // sync re-evaluates the input region now that the modal slot is
    // visible so `Qt::WindowTransparentForInput` flips off.
    syncPassiveShellSurfaceStateForSurface(shellSurface);

    qCInfo(lcOverlay) << "showSnapAssist: screen=" << screenId << "zones=" << emptyZones.size()
                      << "candidates=" << candidates.size();

    // snapAssistShown signal is wired in start.cpp to
    // ensureCancelOverlayShortcutRegistered() - the shell's wl_surface is
    // kbd-None so the per-content QML Shortcut path used by the legacy
    // SnapAssistOverlay can't fire here. KGlobalAccel grab of Escape +
    // cancelSnap()'s existing isSnapAssistVisible() branch routes Escape to
    // hideSnapAssist().
    Q_EMIT snapAssistShown(screenId, emptyZones, candidates);
}

bool OverlayService::setSnapAssistThumbnail(const QString& compositorHandle, int width, int height,
                                            const QByteArray& pixels)
{
    // External entry point. The kwin-effect renders a candidate through
    // KWin's OffscreenQuickScene + WindowThumbnail and posts the result as
    // raw ARGB32 (non-premultiplied) pixels - no PNG encode, no base64.
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
        qCDebug(lcOverlay) << "setSnapAssistThumbnail: pixel-buffer size mismatch - got" << pixels.size() << "expected"
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

bool OverlayService::setWindowThumbnailDmabuf(const QString& compositorHandle, const DmabufThumbnailDesc& desc)
{
    // Experimental zero-copy GPU thumbnail path (Phase-0 spike). Gated behind
    // an env var so the default build behaves exactly as before: the kwin-
    // effect only takes the dma-buf path when its own gate is set, and the
    // daemon refuses it here unless explicitly enabled. Returning false makes
    // the effect fall back to the raw-pixel setSnapAssistThumbnail path, so a
    // preview always appears regardless of dma-buf support.
    static const bool dmabufEnabled = qEnvironmentVariableIsSet("PLASMAZONES_DMABUF_THUMBNAILS");
    if (!dmabufEnabled) {
        return false;
    }
    auto* provider = m_dmabufTextureProvider.load(std::memory_order_acquire);
    if (!provider || desc.fd < 0) {
        return false;
    }
    // Store the descriptor (the provider dups the borrowed fd) and resolve the
    // GPU image:// URL. The actual Vulkan import happens lazily on the render
    // thread when QML loads that URL through the texture provider.
    const QString providerUrl = provider->insert(compositorHandle, desc);
    if (providerUrl.isEmpty()) {
        return false;
    }
    qCDebug(lcOverlay) << "setWindowThumbnailDmabuf: stored dma-buf for" << compositorHandle << desc.width << "x"
                       << desc.height << "fourcc=0x" << QString::number(desc.fourcc, 16);

    // Reveal-gate on the render-completion fence: only push the URL into the
    // live candidate list once the producer's GPU render has finished, so QML
    // never loads (and the render thread never imports) a half-rendered buffer.
    // The wait is event-driven (DmabufFenceWaiter / QSocketNotifier) — no
    // busy-poll, no blocked thread.
    //
    // No-fence branch: the D-Bus boundary currently requires a valid fence fd
    // (OverlayAdaptor rejects an invalid one), so fenceFd < 0 only arises for a
    // hypothetical direct (non-D-Bus) C++ caller; reveal immediately as a
    // defensive fallback.
    if (desc.fenceFd < 0) {
        return applyCandidateThumbnailUrl(compositorHandle, providerUrl);
    }
    const int fenceDup = ::dup(desc.fenceFd);
    if (fenceDup < 0) {
        // Can't watch the fence — reveal now rather than drop the thumbnail.
        return applyCandidateThumbnailUrl(compositorHandle, providerUrl);
    }
    // 1 s bound: a thumbnail render completes in well under a frame; the cap
    // only guards against a producer that crashed mid-render. The
    // DmabufFenceWaiter owns fenceDup and self-deletes on signal or timeout
    // (so per-candidate waiters live at most ~1 s — bounded, not leaked).
    //
    // accepted=true is returned now (deferred-reveal contract: stored, will
    // reveal when the fence signals), unlike the raw-pixel path which returns
    // true only after storing. NOTE the divergence from the dedup re-capture
    // contract: if this fence times out (only on a hung/crashed producer) the
    // reveal is dropped, yet the producer already saw accepted=true and won't
    // re-capture until its recently-posted window rolls past. Accepted as a
    // rare-edge cost of the async reveal. (Unlike the raw-pixel provider, the
    // dma-buf provider is NOT consulted by showSnapAssist's warm-cache pass —
    // only m_thumbnailProvider is — so a dropped reveal is recovered only once
    // the producer re-posts the handle, not from the still-stored descriptor.)
    auto* waiter = new DmabufFenceWaiter(fenceDup, /*timeoutMs=*/1000, this);
    // The reveal is matched by handle against the CURRENT candidate list when it
    // fires (up to ~1 s later): if snap-assist was dismissed, or re-shown for a
    // different window set that doesn't include this handle, applyCandidate-
    // ThumbnailUrl is a harmless no-op. Same window → same thumbnail content.
    connect(waiter, &DmabufFenceWaiter::ready, this, [this, compositorHandle, providerUrl]() {
        applyCandidateThumbnailUrl(compositorHandle, providerUrl);
    });
    return true;
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
    return applyCandidateThumbnailUrl(compositorHandle, providerUrl);
}

bool OverlayService::applyCandidateThumbnailUrl(const QString& compositorHandle, const QString& providerUrl)
{
    // Provider insert succeeded - handle is held even if snap-assist isn't
    // currently open. Provider URL stays valid until eviction. Shared by the
    // raw-pixel (updateSnapAssistCandidateThumbnail) and dma-buf
    // (setWindowThumbnailDmabuf) paths.
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

    // Emit dismissed BEFORE hideSlot so listeners observe the same
    // signal order regardless of whether hideSlot's completion runs
    // synchronously (benign no-op path: lib's hideSlot fires the
    // completion inline, which restores the zone-selector via
    // onSnapAssistSlotHideCompleted) or asynchronously (animator's
    // settle): in both cases "dismissed" arrives first.
    //
    // snapAssistDismissed → WindowDragAdaptor::onSnapAssistDismissed →
    // unregisterCancelOverlayShortcut().
    Q_EMIT snapAssistDismissed();

    auto stateIt = m_screenStates.find(screenId);
    if (stateIt != m_screenStates.end() && stateIt->shell && stateIt->shell->shellSurface()
        && stateIt->snapAssistSlot()) {
        m_shellHost->hideSlot(screenId, PhosphorSlotKeys::SnapAssist(), [this, effectiveId = screenId]() {
            onSnapAssistSlotHideCompleted(effectiveId);
        });
    }

    // Zone-selector restore is owned by onSnapAssistSlotHideCompleted -
    // it fires when the snap-assist slot has finished its hide
    // animation, so the selector fades back in cleanly after the
    // snap-assist has visually cleared. A synchronous restore here
    // would race the in-flight beginHide and rely on the
    // showZoneSelectorSlotOnScreen short-circuit (which checks
    // slot->isVisible() - true mid-animation), risking visible
    // overlap or missed re-shows.

    // Schedule a delayed trim of the thumbnail cache. A rapid re-show
    // (continuation / multi-zone fill / cross-screen move) restarts this
    // timer in showSnapAssist before it fires, preserving the warm cache;
    // a genuine end-of-use lets it fire and release the cached pixels. The
    // cross-screen handoff path routes through onSnapAssistSlotHideCompleted
    // (not here), so monitor switches never start the trim.
    if (!m_snapAssistCacheTrimTimer) {
        m_snapAssistCacheTrimTimer = new QTimer(this);
        m_snapAssistCacheTrimTimer->setSingleShot(true);
        m_snapAssistCacheTrimTimer->setInterval(SnapAssistCacheTrimDelayMs);
        connect(m_snapAssistCacheTrimTimer, &QTimer::timeout, this, [this]() {
            if (auto* provider = m_thumbnailProvider.load(std::memory_order_acquire)) {
                provider->clear();
            }
            if (auto* gpuProvider = m_dmabufTextureProvider.load(std::memory_order_acquire)) {
                gpuProvider->clear();
            }
        });
    }
    m_snapAssistCacheTrimTimer->start();
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
    // prev-screen branch.
    restoreZoneSelectorAfterHide(effectiveId);
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
    QScreen* screen = resolveTargetScreen(m_screenManager, screenId);
    if (!screen) {
        qCWarning(lcOverlay) << "showLayoutPicker: no screen available";
        return;
    }

    const QString resolvedId = screenId.isEmpty() ? PhosphorScreens::ScreenIdentity::identifierFor(screen) : screenId;

    // Same-screen re-request while visible is a no-op; a request for a
    // DIFFERENT screen migrates the picker there (dismiss + reshow below),
    // mirroring showSnapAssist's cross-screen singleton handling instead of
    // silently dropping the request.
    if (m_layoutPickerVisible && m_layoutPickerScreenId == resolvedId) {
        return;
    }

    QRect screenGeom = resolveScreenGeometry(m_screenManager, resolvedId);
    if (!screenGeom.isValid()) {
        screenGeom = screen->geometry();
    }

    auto* state = ensurePassiveShellFor(resolvedId, screen);
    if (!state || !state->shell || !state->shell->shellSurface() || !state->layoutPickerSlot()) {
        qCWarning(lcOverlay) << "showLayoutPicker: no passive shell for screen=" << resolvedId;
        return;
    }

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

    // Hide the zone selector on this VS to avoid overlap. Runs only after
    // every bail above (shell + layouts validated), so a failed request can
    // never leave the drag-time zone selector stuck hidden — same
    // bails-first ordering contract as showSnapAssist.
    hideZoneSelectorSlotOnScreen(resolvedId);

    // The picker is a singleton across screens: with the new target fully
    // validated (shell + layouts), dismiss it on the previous screen before
    // showing here. Animator-driven hideSlot keys only the picker track, so
    // sibling slots on the previous shell keep animating cleanly. Validation
    // failures above return BEFORE this point, leaving the picker untouched
    // on its current screen — same ordering contract as showSnapAssist.
    if (m_layoutPickerVisible && !m_layoutPickerScreenId.isEmpty() && m_layoutPickerScreenId != resolvedId) {
        const QString prevScreenId = m_layoutPickerScreenId;
        auto prevIt = m_screenStates.find(prevScreenId);
        if (prevIt != m_screenStates.end() && prevIt->shell && prevIt->shell->shellSurface()
            && prevIt->layoutPickerSlot()) {
            m_shellHost->hideSlot(prevScreenId, PhosphorSlotKeys::LayoutPicker(), [this, prevScreenId]() {
                onLayoutPickerSlotHideCompleted(prevScreenId);
            });
        }
    }

    const QString activeId = activeLayoutIdForScreen(resolvedId);

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
    writeFontProperties(slot, m_settings, /*includeLabelFontColor=*/false);

    bool locked = false;
    if (m_settings && m_layoutManager) {
        int curDesktop = currentVirtualDesktopForScreen(resolvedId);
        QString curActivity = m_layoutManager->currentActivity();
        locked = isAnyModeLocked(m_settings, m_layoutManager, resolvedId, curDesktop, curActivity);
    }
    writeQmlProperty(slot, QStringLiteral("locked"), locked);
    const PhosphorZones::ContextOverlayOverride overlayOverride = overlayOverrideForScreen(m_layoutManager, resolvedId);
    writeColorSettings(slot, m_settings, &overlayOverride);

    // Stage d: resolve + push the layout-picker surface-shader decoration (same
    // SurfaceDecoration host the OSD uses, retargeted to the "popup.layoutPicker"
    // surface path). Empty source = no decoration (card draws natively).
    applyDecoration(slot, QStringLiteral("popup.layoutPicker"));

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
    m_surfaceAnimator->beginShow(shellSurface, slot, PhosphorRoles::LayoutPicker, []() { });
    // Layout picker is modal - needs input for click-to-select.
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
        // path runs even on idempotent calls (defensive - multiple call
        // sites converge on hideLayoutPicker).
        Q_EMIT layoutPickerDismissed();
        return;
    }

    const QString screenId = m_layoutPickerScreenId;
    m_layoutPickerVisible = false;
    m_layoutPickerScreenId.clear();

    // Emit dismissed BEFORE hideSlot so listeners see "dismissed first,
    // then completion" regardless of whether the completion runs
    // synchronously (benign no-op) or asynchronously (animator settle).
    // Mirrors hideSnapAssist's ordering.
    Q_EMIT layoutPickerDismissed();

    auto stateIt = m_screenStates.find(screenId);
    if (stateIt != m_screenStates.end() && stateIt->shell && stateIt->shell->shellSurface()
        && stateIt->layoutPickerSlot()) {
        m_shellHost->hideSlot(screenId, PhosphorSlotKeys::LayoutPicker(), [this, effectiveId = screenId]() {
            onLayoutPickerSlotHideCompleted(effectiveId);
        });
    }

    // Zone-selector restore is owned by onLayoutPickerSlotHideCompleted -
    // mirror of the snap-assist + OSD ownership pattern. The synchronous
    // fast-path raced the in-flight beginHide.
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
    // Symmetric restore - see onSnapAssistSlotHideCompleted /
    // onOsdSlotHideCompleted. The picker hid the zone-selector slot
    // on show; restore it once the picker has finished its hide.
    restoreZoneSelectorAfterHide(effectiveId);
    syncPassiveShellSurfaceState(effectiveId);
}

void OverlayService::onLayoutPickerDismissRequested()
{
    // Backdrop click forwarded from the shell - same as Escape route.
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
