// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "internal.h"
#include "daemon/overlayservice.h"
#include "core/platform/logging.h"
#include "phosphor_slot_keys.h"
#include <PhosphorOverlay/ShellHost.h>
#include <PhosphorSurfaces/SurfaceManager.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutUtils.h>
#include "core/utils/utils.h"
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/VirtualScreen.h>
#include "daemon/rendering/snapassistthumbnailprovider.h"
#include "daemon/rendering/dmabuftextureprovider.h"
#include "daemon/rendering/dmabuffencewaiter.h"
#include <PhosphorLayoutApi/LayoutId.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QLoggingCategory>
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

Q_DECLARE_LOGGING_CATEGORY(lcSnapAssistTrace)

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
    static const bool traceEnabled = PhosphorProtocol::Service::snapAssistThumbnailTraceEnabled();
    QElapsedTimer showTimer;
    if (traceEnabled) {
        showTimer.start();
    }
    // Bail paths emit dismissed only when snap-assist is NOT currently
    // visible: the emit exists so a failed show releases an idle Escape
    // grab, but when snap-assist is live on ANOTHER screen an unconditional
    // emit dropped that instance's grab while it stayed visible (and
    // isSnapAssistVisible() still returned true), leaving it dismissable
    // only by backdrop click.
    const auto bailDismiss = [this]() {
        if (!m_snapAssistVisible) {
            Q_EMIT snapAssistDismissed();
        }
    };

    // Master-toggle gate at the service boundary too, not only in
    // OverlayAdaptor: IOverlayService is a public C++ API and direct
    // (non-D-Bus) callers may exist — the same posture
    // setSnapAssistThumbnail takes for shape validation.
    if (m_settings && !m_settings->snapAssistFeatureEnabled()) {
        qCDebug(lcOverlay) << "showSnapAssist: feature disabled";
        bailDismiss();
        return;
    }

    if (emptyZones.isEmpty() || candidates.isEmpty()) {
        qCDebug(lcOverlay) << "showSnapAssist: no empty zones or candidates";
        bailDismiss();
        return;
    }

    QScreen* screen = resolveTargetScreen(m_screenManager, screenId);
    if (!screen) {
        qCWarning(lcOverlay) << "showSnapAssist: no screen available";
        bailDismiss();
        return;
    }

    // Resolve an empty id to the actual screen's identifier (mirrors
    // showLayoutPicker): resolveTargetScreen falls back to the primary
    // screen for an empty/unknown id, and storing the EMPTY id verbatim in
    // m_snapAssistScreenId dead-ended every subsequent path — hideSnapAssist
    // early-returns on an empty id, so the modal became unhideable and the
    // shared Escape grab leaked.
    const QString resolvedId = screenId.isEmpty() ? PhosphorScreens::ScreenIdentity::identifierFor(screen) : screenId;

    const QVariantList zonesList = emptyZonesToVariantList(emptyZones);

    // Stale-request guard: KWin effect computes empty zones async; layout
    // may have switched in between. Verify at least one zone still exists.
    // The guard is SKIPPED for engine-owned contexts (autotile/scrolling):
    // those have no snapping Layout of their own, and every resolver in
    // reach falls back to the default layout for them (layoutForScreen
    // itself ends in `: defaultLayout()`), so the comparison would test the
    // request's zone ids against a layout the screen is not using and
    // discard a live request as stale. Normal flow never shows snap-assist
    // there (the effect gates trigger sites on !isManagedScreen), so the
    // skip only widens acceptance for defensive direct callers.
    PhosphorZones::Layout* currentLayout = nullptr;
    if (m_layoutManager && !resolvedId.isEmpty()) {
        const QString assignmentId = m_layoutManager->assignmentIdForScreen(
            resolvedId, currentVirtualDesktopForScreen(resolvedId), m_currentActivity);
        const bool engineOwned =
            PhosphorLayout::LayoutId::isAutotile(assignmentId) || PhosphorLayout::LayoutId::isScrolling(assignmentId);
        if (!engineOwned) {
            currentLayout = m_layoutManager->layoutForScreen(resolvedId, currentVirtualDesktopForScreen(resolvedId),
                                                             m_currentActivity);
        }
    }
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
            bailDismiss();
            return;
        }
    }

    QRect screenGeom = resolveScreenGeometry(m_screenManager, resolvedId);
    if (!screenGeom.isValid()) {
        screenGeom = screen->geometry();
    }

    // Resolve target shell - per-screen shell hosts the snap-assist slot.
    auto* state = ensurePassiveShellFor(resolvedId, screen);
    if (!state || !state->shell || !state->shell->shellSurface() || !state->snapAssistSlot()) {
        qCWarning(lcOverlay) << "showSnapAssist: no passive shell for screen=" << resolvedId;
        bailDismiss();
        return;
    }

    // At most one Escape-consuming modal at a time — the cheatsheet states
    // and enforces this invariant for itself; snap-assist and the picker
    // must uphold it against each other too, or two modal backdrops stack
    // on one surface with an order-dependent Escape-grab release.
    if (m_layoutPickerVisible) {
        hideLayoutPicker();
    }

    const bool sameScreenRefresh = m_snapAssistVisible && m_snapAssistScreenId == resolvedId;

    // Snap-assist is in active use again - cancel any pending cache trim so
    // the continuation lookups below (urlFor) hit the warm caches.
    if (m_snapAssistCacheTrimTimer) {
        m_snapAssistCacheTrimTimer->stop();
    }

    // Build the static-per-show candidate model and the warm thumbnail map.
    // Thumbnails live in their own map (pushed as a separate QML property)
    // so late arrivals never rebuild the candidate delegates. The warm pass
    // consults BOTH providers: the raw-pixel LRU and the dma-buf store —
    // omitting the latter stranded every zero-copy candidate on its icon
    // from the second show onward (the producer's dedup skip suppressed the
    // re-post that was supposed to recover it).
    //
    // Hoist the atomic loads out of the loop — they're stable across the
    // single-threaded GUI iteration here.
    m_snapAssistCandidates = candidatesToVariantList(candidates);
    m_snapAssistThumbnails.clear();
    int cachedCount = 0;
    auto* const thumbProvider = m_thumbnailProvider.load(std::memory_order_acquire);
    auto* const gpuProvider = m_dmabufTextureProvider.load(std::memory_order_acquire);
    for (const auto& candVariant : std::as_const(m_snapAssistCandidates)) {
        const QString compositorHandle = candVariant.toMap().value(QStringLiteral("compositorHandle")).toString();
        if (compositorHandle.isEmpty()) {
            continue;
        }
        QString cachedUrl = thumbProvider ? thumbProvider->urlFor(compositorHandle) : QString();
        if (cachedUrl.isEmpty() && gpuProvider) {
            // urlFor only serves REVEALED entries (render fence signaled),
            // so a warm dma-buf URL is always safe to hand to QML.
            cachedUrl = gpuProvider->urlFor(compositorHandle);
        }
        if (!cachedUrl.isEmpty()) {
            m_snapAssistThumbnails.insert(compositorHandle, cachedUrl);
            ++cachedCount;
        }
    }
    qCDebug(lcOverlay) << "showSnapAssist:" << cachedCount << "cached thumbnails;"
                       << "remaining will arrive from kwin-effect via setSnapAssistThumbnail";

    auto* slot = state->snapAssistSlot();
    auto* shellSurface = state->shell->shellSurface();
    auto* shellWindow = state->shell->shellWindow();

    writeQmlProperty(slot, QStringLiteral("emptyZones"), zonesList);
    writeQmlProperty(slot, QStringLiteral("candidates"), m_snapAssistCandidates);
    writeQmlProperty(slot, QStringLiteral("thumbnails"), m_snapAssistThumbnails);

    const PhosphorZones::ContextOverlayOverride overlayOverride = overlayOverrideForScreen(m_layoutManager, resolvedId);
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
    // Runs on the in-place refresh path too, so a shader/rule edit made
    // while snap-assist is up takes effect on the next continuation rather
    // than only on the next full show.
    applyDecoration(slot, QStringLiteral("popup.snapAssist"));

    if (sameScreenRefresh) {
        // In-place refresh: the overlay is already up on this screen (a
        // continuation snapping into the next zone). Re-running the full
        // show ceremony tore down and rebuilt the Loader content and
        // visibly restarted the entrance animation on every continuation —
        // the sibling showLayoutPicker refreshes in place for the same
        // reason. The property writes above updated zones, candidates and
        // thumbnails; the zone selector is already hidden and the shell
        // already sized/shown.
        qCInfo(lcOverlay) << "showSnapAssist: refreshed in place on screen=" << resolvedId
                          << "zones=" << emptyZones.size() << "candidates=" << candidates.size();
        Q_EMIT snapAssistShown(resolvedId, emptyZones, candidates);
        return;
    }

    // If snap-assist is currently shown on a DIFFERENT screen, dismiss
    // it there first - snap-assist is a singleton across all screens.
    // Animator-driven beginHide on (prevSurface, prevSlot,
    // PhosphorRoles::SnapAssist) keys ONLY the snap-assist track via the
    // per-(Surface, target) animator keying - sibling slots on the
    // same shell (OSD, zone-selector) keep animating cleanly. hideSlot is
    // called UNCONDITIONALLY: the lib validates state itself and fires the
    // completion synchronously on every benign no-op branch, which is what
    // resets the previous slot and restores its zone selector — a
    // caller-side guard re-checking the same conditions defeated exactly
    // that completion when the shell surface was already gone.
    //
    // Latch the new screen BEFORE issuing the hide: on the lib's synchronous
    // completion branches, onSnapAssistSlotHideCompleted(prev) runs inline
    // and its zone-selector restore bails while snap-assist is visible ON
    // THAT screen — with the latch still naming prev, the bail wrongly
    // suppressed the old screen's restore.
    const QString prevScreenId =
        (m_snapAssistVisible && !m_snapAssistScreenId.isEmpty() && m_snapAssistScreenId != resolvedId)
        ? m_snapAssistScreenId
        : QString();
    m_snapAssistScreenId = resolvedId;
    m_snapAssistVisible = true;
    if (!prevScreenId.isEmpty()) {
        m_shellHost->hideSlot(prevScreenId, PhosphorSlotKeys::SnapAssist(), [this, prevScreenId]() {
            onSnapAssistSlotHideCompleted(prevScreenId);
        });
    }

    // Hide the zone selector for the specific virtual screen where snap
    // assist is showing - selectors on adjacent VS of the same physical
    // monitor stay visible. Reset cursor state first so a re-show after
    // dismiss doesn't tick edge-scroll on stale drag-time cursor coords.
    hideZoneSelectorSlotOnScreen(resolvedId);

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

    qCInfo(lcOverlay) << "showSnapAssist: screen=" << resolvedId << "zones=" << emptyZones.size()
                      << "candidates=" << candidates.size();
    if (traceEnabled) {
        // Main-thread time the show handler itself holds. Thumbnail posts
        // arriving while it runs queue behind it, so this is the floor of
        // their D-Bus round-trip during a show.
        qCInfo(lcSnapAssistTrace).nospace() << "show zones=" << emptyZones.size() << " candidates=" << candidates.size()
                                            << " handler=" << showTimer.nsecsElapsed() / 1000 << "us";
    }

    // snapAssistShown signal is wired in shortcuts_wiring.cpp to
    // ensureCancelOverlayShortcutRegistered() - the shell's wl_surface is
    // kbd-None so the per-content QML Shortcut path used by the legacy
    // SnapAssistOverlay can't fire here. KGlobalAccel grab of Escape +
    // cancelSnap()'s existing isSnapAssistVisible() branch routes Escape to
    // hideSnapAssist().
    Q_EMIT snapAssistShown(resolvedId, emptyZones, candidates);
}

bool OverlayService::setSnapAssistThumbnail(const QString& compositorHandle, int width, int height,
                                            const QByteArray& pixels)
{
    // External entry point. The kwin-effect renders a candidate directly
    // into an offscreen GLFramebuffer (effects->drawWindow) and posts the
    // readback as raw ARGB32 (non-premultiplied) pixels - no PNG encode, no
    // base64.
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
    // Bounds: a 256² thumbnail is the steady-state size; the shared
    // protocol ceiling is the cap. Anything larger is almost certainly a
    // marshalling bug or a hostile sender that slipped past auth, and
    // consumes excessive bytes to round-trip through the cache.
    static constexpr int MaxDimension = PhosphorProtocol::Service::SnapAssistThumbnailMaxDimension;
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
    static const bool traceEnabled = PhosphorProtocol::Service::snapAssistThumbnailTraceEnabled();
    QElapsedTimer receiveTimer;
    if (traceEnabled) {
        receiveTimer.start();
    }
    QImage view(reinterpret_cast<const uchar*>(pixels.constData()), width, height, width * 4, QImage::Format_ARGB32);
    QImage image = view.copy();
    const qint64 copyUs = traceEnabled ? receiveTimer.nsecsElapsed() / 1000 : 0;
    const bool accepted = updateSnapAssistCandidateThumbnail(compositorHandle, std::move(image));
    if (traceEnabled) {
        // copy = detaching the D-Bus byte buffer into an owned QImage; total
        // adds the provider insert and the candidate-map/QML push.
        qCInfo(lcSnapAssistTrace).nospace()
            << "receive " << compositorHandle << " " << width << "x" << height << " payload=" << pixels.size()
            << "B copy=" << copyUs << "us total=" << receiveTimer.nsecsElapsed() / 1000 << "us accepted=" << accepted;
    }
    return accepted;
}

bool OverlayService::setWindowThumbnailDmabuf(const QString& compositorHandle, const DmabufThumbnailDesc& desc)
{
    // Experimental zero-copy GPU thumbnail path (Phase-0 spike). Gated behind
    // an env var so the default build behaves exactly as before: the kwin-
    // effect only takes the dma-buf path when its own gate is set, and the
    // daemon refuses it here unless explicitly enabled. Returning false makes
    // the effect fall back to the raw-pixel setSnapAssistThumbnail path, so a
    // preview always appears regardless of dma-buf support.
    static const bool dmabufEnabled = PhosphorProtocol::Service::snapAssistDmabufThumbnailsEnabled();
    if (!dmabufEnabled) {
        return false;
    }
    auto* provider = m_dmabufTextureProvider.load(std::memory_order_acquire);
    if (!provider || desc.fd < 0) {
        return false;
    }
    // Re-validate shape at the service boundary, mirroring the raw-pixel
    // sibling and for the same reason (IOverlayService is a public C++ API;
    // the adaptor's checks only cover the D-Bus path). Beyond dimensions,
    // bound stride/offset: the Vulkan importer feeds them into
    // VkSubresourceLayout unchecked, and a stride below the row's real byte
    // count can only be a corrupt descriptor.
    static constexpr int MaxDimension = PhosphorProtocol::Service::SnapAssistThumbnailMaxDimension;
    if (desc.width <= 0 || desc.height <= 0 || desc.width > MaxDimension || desc.height > MaxDimension
        || desc.stride < static_cast<uint32_t>(desc.width) * 4 || desc.stride > (1u << 30)
        || desc.offset > (1u << 30)) {
        qCDebug(lcOverlay) << "setWindowThumbnailDmabuf: invalid descriptor shape" << desc.width << "x" << desc.height
                           << "stride=" << desc.stride << "offset=" << desc.offset << "for" << compositorHandle;
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
    // defensive fallback. markRevealed also unlocks the provider's urlFor so
    // the warm-cache pass can re-serve this entry on a later show.
    if (desc.fenceFd < 0) {
        provider->markRevealed(compositorHandle, providerUrl);
        return applyCandidateThumbnailUrl(compositorHandle, providerUrl);
    }
    const int fenceDup = ::dup(desc.fenceFd);
    if (fenceDup < 0) {
        // Can't watch the fence — reveal now rather than drop the thumbnail.
        // (This is the one branch where a theoretically mid-render buffer can
        // reach QML; under fd exhaustion, showing a torn frame beats showing
        // nothing.)
        provider->markRevealed(compositorHandle, providerUrl);
        return applyCandidateThumbnailUrl(compositorHandle, providerUrl);
    }
    // 1 s bound: a thumbnail render completes in well under a frame; the cap
    // only guards against a producer that crashed mid-render. The
    // DmabufFenceWaiter owns fenceDup and self-deletes on signal or timeout
    // (so per-candidate waiters live at most ~1 s — bounded, not leaked).
    //
    // accepted=true is returned now (deferred-reveal contract: stored, will
    // reveal when the fence signals), unlike the raw-pixel path which returns
    // true only after storing. If the fence times out (only on a hung or
    // crashed producer) the reveal is dropped for THIS show; the entry stays
    // un-revealed, so the warm-cache pass will not serve it either, and
    // recovery happens when the producer re-posts after its own daemon-ready
    // or cache-trim dedup reset.
    auto* waiter = new DmabufFenceWaiter(fenceDup, /*timeoutMs=*/1000, this);
    // The reveal is matched by handle against the CURRENT candidate list when it
    // fires (up to ~1 s later): if snap-assist was dismissed, or re-shown for a
    // different window set that doesn't include this handle, applyCandidate-
    // ThumbnailUrl is a harmless no-op. Same window → same thumbnail content.
    // markRevealed runs regardless so the stored descriptor becomes warm-cache
    // servable once its render is provably complete.
    connect(waiter, &DmabufFenceWaiter::ready, this, [this, compositorHandle, providerUrl]() {
        // providerUrl names the generation THIS fence belongs to; the
        // provider applies the reveal only if that generation is still the
        // stored one, so a late fence from a superseded post cannot reveal
        // a newer, still-rendering buffer.
        if (auto* gpuProvider = m_dmabufTextureProvider.load(std::memory_order_acquire)) {
            gpuProvider->markRevealed(compositorHandle, providerUrl);
        }
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
    auto stateIt = m_screenStates.constFind(m_snapAssistScreenId);
    if (stateIt == m_screenStates.constEnd()) {
        return true;
    }
    auto* slot = stateIt->snapAssistSlot();
    if (!slot) {
        return true;
    }
    // Update the standalone thumbnails map and push ONLY it: the candidate
    // list itself stays untouched (its Repeater delegates were built once at
    // show time), so an arriving thumbnail re-evaluates just the Image
    // bindings reading the map — no delegate teardown, no hover reset, no
    // dropped in-flight press. Gate on membership in the current show so the
    // per-show map can't accumulate handles from a stale show.
    for (const auto& candVariant : std::as_const(m_snapAssistCandidates)) {
        if (candVariant.toMap().value(QStringLiteral("compositorHandle")).toString() == compositorHandle) {
            m_snapAssistThumbnails.insert(compositorHandle, providerUrl);
            writeQmlProperty(slot, QStringLiteral("thumbnails"), m_snapAssistThumbnails);
            qCDebug(lcOverlay) << "SnapAssist: thumbnail updated for" << compositorHandle;
            break;
        }
    }
    return true;
}

void OverlayService::hideSnapAssist()
{
    if (!m_snapAssistVisible || m_snapAssistScreenId.isEmpty()) {
        // Always emit dismissed so the daemon's Escape-shortcut release
        // path runs even on idempotent calls (defensive - multiple call
        // sites converge here, and a teardown reset may have flipped the
        // flag before a converging caller arrives). Mirrors
        // hideLayoutPicker's documented contract; the release is
        // conditional on no other overlay holding the grab, so a spurious
        // emit is harmless.
        Q_EMIT snapAssistDismissed();
        return;
    }

    const QString screenId = m_snapAssistScreenId;
    m_snapAssistCandidates.clear();
    m_snapAssistThumbnails.clear();
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
    // releaseCancelOverlayShortcutIfIdle(), which drops the shared Escape
    // grab only when no other overlay (e.g. the layout picker) still
    // holds it - the conditionality is load-bearing for bail-path safety.
    Q_EMIT snapAssistDismissed();

    // hideSlot is called UNCONDITIONALLY — no caller-side shell/slot guard.
    // The lib validates its own state and fires the completion synchronously
    // on every benign no-op branch precisely so consumer parallel state
    // (slot visibility, `loaded`, the zone-selector restore) never sticks
    // "live"; re-checking the same conditions here defeated that completion
    // whenever the shell surface was already gone (compositor-loss
    // recovery), leaving the selector permanently hidden mid-drag.
    m_shellHost->hideSlot(screenId, PhosphorSlotKeys::SnapAssist(), [this, effectiveId = screenId]() {
        onSnapAssistSlotHideCompleted(effectiveId);
    });

    // Zone-selector restore is owned by onSnapAssistSlotHideCompleted -
    // it fires when the snap-assist slot has finished its hide
    // animation, so the selector fades back in cleanly after the
    // snap-assist has visually cleared. A synchronous restore here
    // would race the in-flight beginHide and rely on the
    // showZoneSelectorSlotOnScreen short-circuit (which checks
    // slot->isVisible() - true mid-animation), risking visible
    // overlap or missed re-shows.

    // Schedule a delayed trim of the thumbnail caches. A rapid re-show
    // (continuation / multi-zone fill / cross-screen move) restarts the
    // timer in showSnapAssist before it fires, preserving the warm caches;
    // a genuine end-of-use lets it fire and release the cached pixels. The
    // cross-screen handoff path routes through onSnapAssistSlotHideCompleted
    // (not here), so monitor switches never start the trim.
    scheduleSnapAssistCacheTrim();
}

void OverlayService::scheduleSnapAssistCacheTrim()
{
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
            // Tell the producer its residency assumptions just went cold.
            // The kwin-effect mirrors this cache with a recently-posted
            // dedup set whose skip path re-promotes entries; without this
            // signal the trim desynchronised the two sides PERMANENTLY —
            // the effect skipped re-capture for handles the daemon no
            // longer held, and snap-assist fell back to program icons until
            // a daemon restart. OverlayAdaptor relays it onto the bus.
            Q_EMIT snapAssistThumbnailCacheTrimmed();
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

    // Same-screen re-request while visible is a visual no-op; a request for a
    // DIFFERENT screen migrates the picker there (dismiss + reshow below),
    // mirroring showSnapAssist's cross-screen singleton handling instead of
    // silently dropping the request. The highlight can still have gone stale
    // under the open picker (a quick-slot press swaps the template or layout
    // without closing it), so re-push the active id before bailing.
    if (m_layoutPickerVisible && m_layoutPickerScreenId == resolvedId) {
        auto it = m_screenStates.find(resolvedId);
        if (it != m_screenStates.end() && it->shell && it->layoutPickerSlot()) {
            writeQmlProperty(it->layoutPickerSlot(), QStringLiteral("activeLayoutId"),
                             activeLayoutIdForScreen(resolvedId));
        }
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
        // Silent bail by design: the only caller (Daemon's layoutPickerRequested
        // handler) already checks visibleLayoutCount and shows the OSD for the
        // empty case, including the Templates-specific "no column templates"
        // card, so anything reaching here is a race against a store mutation
        // rather than something to tell the user about.
        qCDebug(lcOverlay) << "showLayoutPicker: no layouts available";
        return;
    }

    // At most one Escape-consuming modal at a time — same invariant the
    // cheatsheet enforces (see showCheatsheet) and showSnapAssist now
    // upholds in the other direction. Runs only after every bail above so a
    // failed picker request never tears down a live snap-assist.
    if (m_snapAssistVisible) {
        hideSnapAssist();
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
    // hideSlot is unconditional: the lib validates state itself and fires
    // the completion inline on benign no-ops, which is what resets the
    // previous slot; a caller-side guard defeated that (see hideSnapAssist).
    //
    // Latch the singleton state BEFORE the prev-screen hide AND the property
    // pushes below, mirroring showSnapAssist, for two reasons: the lib's
    // synchronous completion branches run onLayoutPickerSlotHideCompleted
    // inline and its zone-selector restore keys off which screen the picker
    // is NOW on; and OverlayService is a QML context property, so a binding
    // re-entering the service mid-push must observe the picker as
    // visible-on-this-screen or its hideLayoutPicker takes the idempotent
    // branch and the picker shows with nothing recording it.
    const QString prevPickerScreenId =
        (m_layoutPickerVisible && !m_layoutPickerScreenId.isEmpty() && m_layoutPickerScreenId != resolvedId)
        ? m_layoutPickerScreenId
        : QString();
    m_layoutPickerScreenId = resolvedId;
    m_layoutPickerVisible = true;
    if (!prevPickerScreenId.isEmpty()) {
        m_shellHost->hideSlot(prevPickerScreenId, PhosphorSlotKeys::LayoutPicker(), [this, prevPickerScreenId]() {
            onLayoutPickerSlotHideCompleted(prevPickerScreenId);
        });
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
    // Auto badges advertise snap-to-empty-zone; suppressed on Templates
    // screens where that behaviour cannot happen (see showLayoutOsdImpl).
    const bool templatesScreenForBadges =
        m_layoutSupportResolver && m_layoutSupportResolver(resolvedId) == LayoutSupportTemplates;
    writeQmlProperty(slot, QStringLiteral("globalAutoAssign"),
                     !templatesScreenForBadges && m_settings && m_settings->autoAssignAllLayouts());
    writeFontProperties(slot, m_settings, /*includeLabelFontColor=*/false);

    bool locked = false;
    if (m_settings && m_layoutManager) {
        int curDesktop = currentVirtualDesktopForScreen(resolvedId);
        QString curActivity = m_layoutManager->currentActivity();
        // Pass the LIVE mode lens (see pickerLockModeFor) — shared with the
        // live lock re-push in refreshContextLockState so the two cannot
        // disagree about which mode's lock the picker is showing.
        locked = isAnyModeLocked(m_settings, m_layoutManager, resolvedId, curDesktop, curActivity,
                                 pickerLockModeFor(resolvedId));
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

    // Unconditional for the same reason as hideSnapAssist: the lib's
    // benign-no-op completion is what resets the slot and restores the zone
    // selector, and a caller-side guard defeated it on the exact branches
    // (dead shell surface) it exists for.
    m_shellHost->hideSlot(screenId, PhosphorSlotKeys::LayoutPicker(), [this, effectiveId = screenId]() {
        onLayoutPickerSlotHideCompleted(effectiveId);
    });

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
    // Capture the bound screen BEFORE the hide clears it — the consumer
    // re-binds the layout controller to this screen so the pick cannot
    // land on a screen a mid-pick desktop switch retargeted.
    const QString boundScreenId = m_layoutPickerScreenId;
    hideLayoutPicker();
    Q_EMIT layoutPickerSelected(layoutId, boundScreenId);
}

void OverlayService::pickerMoveSelection(int dx, int dy)
{
    if (!m_layoutPickerVisible || m_layoutPickerScreenId.isEmpty()) {
        return;
    }
    // constFind, not value(): value() copies the whole ScreenState struct
    // just to call a one-line accessor.
    auto it = m_screenStates.constFind(m_layoutPickerScreenId);
    auto* slot = (it != m_screenStates.constEnd()) ? it->layoutPickerSlot() : nullptr;
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
    auto it = m_screenStates.constFind(m_layoutPickerScreenId);
    auto* slot = (it != m_screenStates.constEnd()) ? it->layoutPickerSlot() : nullptr;
    if (!slot) {
        return;
    }
    QMetaObject::invokeMethod(slot, "confirmSelection");
}

} // namespace PlasmaZones
