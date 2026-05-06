// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "internal.h"
#include "../overlayservice.h"
#include "../../core/logging.h"
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

/// Convert EmptyZoneList to QVariantList for QML property push
static QVariantList emptyZonesToVariantList(const EmptyZoneList& zones)
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
static QVariantList candidatesToVariantList(const SnapAssistCandidateList& candidates)
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

void OverlayService::showSnapAssist(const QString& screenId, const EmptyZoneList& emptyZones,
                                    const SnapAssistCandidateList& candidates)
{
    if (emptyZones.isEmpty() || candidates.isEmpty()) {
        qCDebug(lcOverlay) << "showSnapAssist: no empty zones or candidates";
        Q_EMIT snapAssistDismissed(); // Notify listeners that snap assist won't show
        return;
    }

    // Resolve physical screen using shared helper (handles virtual IDs, fallback chain)
    QScreen* screen = resolveTargetScreen(m_screenManager, screenId);
    if (!screen) {
        qCWarning(lcOverlay) << "showSnapAssist: no screen available";
        Q_EMIT snapAssistDismissed();
        return;
    }

    // Convert typed lists to QVariantLists for QML property push.
    const QVariantList zonesList = emptyZonesToVariantList(emptyZones);
    QVariantList candidatesList = candidatesToVariantList(candidates);

    // Guard against stale snap assist requests from a previous layout.
    // The KWin effect computes empty zones asynchronously; by the time the D-Bus
    // request arrives, the layout may have been switched and the zone IDs are no
    // longer valid. Verify that at least one requested zone exists in the current
    // layout for the target screen.
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

    // Use virtual screen geometry when available, otherwise physical
    QRect screenGeom = resolveScreenGeometry(m_screenManager, screenId);
    if (!screenGeom.isValid()) {
        screenGeom = screen->geometry();
    }

    // Reuse the warmed / previous QQuickWindow when on the same screen
    // (avoids QML compilation + Wayland surface create/destroy churn).
    // Recreate when the target screen changes or the window was destroyed
    // outright (m_snapAssistWindow auto-nulls via the QObject::destroyed
    // hook installed at createSnapAssistWindowFor).
    //
    // Note: NOT gated on `isVisible()` any more. Pre-warming the surface
    // (via warmUpSnapAssist) leaves it Qt-visible=false after the prime
    // cycle's hide; user-triggered hides also drop into hide() (rather
    // than destroying outright), which unmaps the wl_surface but keeps
    // the QQuickWindow alive. In both cases the window object is reusable
    // — show() re-maps the wl_surface and the existing scene graph is
    // good to render. The earlier "isVisible() == swapchain is alive"
    // heuristic was over-cautious; QQuickWindow's swapchain handling
    // tolerates remap on a visible-cycled window without explicit teardown.
    const bool reuseWindow = m_snapAssistWindow && m_snapAssistScreenId == screenId;
    if (!reuseWindow) {
        destroySnapAssistWindow();
        createSnapAssistWindowFor(screen, screenGeom, screenId);
        if (!m_snapAssistWindow) {
            Q_EMIT snapAssistDismissed();
            return;
        }
    }

    m_snapAssistScreen = screen;
    m_snapAssistScreenId = screenId;

    // Hide the zone selector only for the specific virtual screen where snap assist is showing.
    // Snap assist now uses virtual-screen geometry (not full physical monitor coverage), so
    // selectors on adjacent virtual screens of the same physical monitor should remain visible.
    //
    // Route through Surface::hide() so the SurfaceAnimator drives the fade-out
    // and the keepMappedOnHide=true selector keeps its warmed Vulkan swapchain
    // for the next drag-near-edge cycle. Calling QQuickWindow::hide() directly
    // would unmap the wl_surface and defeat the warm-surface optimisation
    // createZoneSelectorWindow opted into.
    //
    // Reset the QML hover state BEFORE hiding so when snap-assist later
    // re-shows the selector via hideSnapAssist, autoScrollTimer doesn't
    // tick on cursor coords frozen from the drag that triggered snap-assist
    // — that would cause an unwanted edge-scroll the moment the selector
    // reappears. Mirrors the reset hideZoneSelector does on the explicit-
    // hide path.
    {
        const auto& selectorState = m_screenStates.value(screenId);
        if (selectorState.zoneSelectorSurface) {
            if (selectorState.zoneSelectorWindow) {
                QMetaObject::invokeMethod(selectorState.zoneSelectorWindow, "resetCursorState");
            }
            selectorState.zoneSelectorSurface->hide();
        }
    }

    // Capture is driven by the kwin-effect: it grabs each candidate via
    // OffscreenQuickScene + org.kde.kwin's WindowThumbnail and posts each
    // thumbnail back via setSnapAssistThumbnail. Here we just attach any
    // already-cached URL so the overlay's first paint shows live thumbnails
    // for windows the user has snap-assisted to recently. Misses fall back
    // to the icon path in QML; the effect's per-candidate D-Bus calls
    // arrive shortly after and update the candidate list in place.
    //
    // Build the list locally and swap at the end. m_snapAssistCandidates
    // is read by updateSnapAssistCandidateThumbnail (D-Bus dispatch path);
    // an interleaved iteration over a partially-built list would observe
    // half-populated candidates. Single-thread today (D-Bus + QML both
    // dispatch on the main thread) makes the interleave impossible, but
    // a future setSnapAssistThumbnail invoked via QueuedConnection from
    // a worker would break that invariant. Building locally pins the
    // contract regardless of dispatch threading.
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

    writeQmlProperty(m_snapAssistWindow, QStringLiteral("emptyZones"), zonesList);
    writeQmlProperty(m_snapAssistWindow, QStringLiteral("candidates"), m_snapAssistCandidates);
    writeQmlProperty(m_snapAssistWindow, QStringLiteral("screenWidth"), screenGeom.width());
    writeQmlProperty(m_snapAssistWindow, QStringLiteral("screenHeight"), screenGeom.height());

    // PhosphorZones::Zone appearance defaults (used when zone.useCustomColors is false) - match main overlay
    writeColorSettings(m_snapAssistWindow, m_settings);
    if (m_settings) {
        writeQmlProperty(m_snapAssistWindow, QStringLiteral("borderWidth"), m_settings->borderWidth());
        writeQmlProperty(m_snapAssistWindow, QStringLiteral("borderRadius"), m_settings->borderRadius());
    }

    // On reuse, defensively re-assert the Exclusive keyboard grab. The fresh-
    // create path always attaches with Exclusive via PzRoles::SnapAssist, but
    // the grab is mutable on a live wl_surface; any external code path that
    // ever drops it (today: none — but cheap insurance) would otherwise leave
    // the reused window unable to receive Escape / Enter. Idempotent.
    if (reuseWindow && m_snapAssistSurface) {
        if (auto* handle = m_snapAssistSurface->transport()) {
            handle->setKeyboardInteractivity(PhosphorLayer::KeyboardInteractivity::Exclusive);
        }
    }

    if (!reuseWindow) {
        assertWindowOnScreen(m_snapAssistWindow, screen, screenGeom);
        m_snapAssistWindow->setWidth(screenGeom.width());
        m_snapAssistWindow->setHeight(screenGeom.height());
    }
    // Skip surface->show() when reusing an already-Shown surface. Surface
    // treats Shown→Shown as a re-trigger and replays the
    // popup.snapAssist.show fade-in (cancel + fresh beginShow with
    // fromOpacity=0). For the
    // continuation pattern — user finishes one snap, the next snap-assist
    // reuses the warmed window — that flash is a regression of the
    // warm-surface optimisation. Property writes already updated the
    // candidates/zones; nothing about the visual state needs to replay.
    // First-show on a freshly-created surface (reuseWindow=false) and
    // re-show on a previously-hidden surface both still dispatch normally.
    if (m_snapAssistSurface && (!reuseWindow || !m_snapAssistSurface->isLogicallyShown())) {
        // OSD-style content lifecycle: toggle `loaded` false→true so
        // the SnapAssistOverlay Loader re-instantiates the popup body
        // and produces a fresh shaderAnchor QQuickItem on every show.
        // See ZoneSelectorWindow's `loaded` property docstring for the
        // full rationale (avoids stale FBO content on subsequent
        // vertex-shader transitions when the persistent anchor's
        // QQuickItemLayer state survives across shows).
        writeQmlProperty(m_snapAssistWindow, QStringLiteral("loaded"), false);
        writeQmlProperty(m_snapAssistWindow, QStringLiteral("loaded"), true);
        cancelSurfacePrime(m_snapAssistSurface);
        m_snapAssistSurface->show();
    }
    // Ensure the window receives keyboard focus for Escape handling on Wayland.
    // KeyboardInteractivityExclusive tells the compositor to send keyboard events,
    // but Qt may not set internal focus without an explicit activation request.
    m_snapAssistWindow->requestActivate();
    qCInfo(lcOverlay) << "showSnapAssist: screen=" << screenId << "zones=" << emptyZones.size()
                      << "candidates=" << candidates.size() << "reuse=" << reuseWindow;

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
    // Cache insert succeeded — from the caller's perspective the daemon
    // holds the handle even if the snap-assist overlay isn't currently
    // open. The provider URL stays valid until LRU eviction; a future
    // showSnapAssist will pick it up via urlFor().
    if (!m_snapAssistWindow || !m_snapAssistWindow->isVisible()) {
        return true;
    }
    for (int i = 0; i < m_snapAssistCandidates.size(); ++i) {
        QVariantMap cand = m_snapAssistCandidates[i].toMap();
        if (cand.value(QStringLiteral("compositorHandle")).toString() == compositorHandle) {
            cand[QStringLiteral("thumbnail")] = providerUrl;
            m_snapAssistCandidates[i] = cand;
            writeQmlProperty(m_snapAssistWindow, QStringLiteral("candidates"), m_snapAssistCandidates);
            qCDebug(lcOverlay) << "SnapAssist: thumbnail updated for" << compositorHandle;
            break;
        }
    }
    return true;
}

bool OverlayService::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_snapAssistWindow && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            // Defer destruction to avoid deleting the window from within its own event handler
            QTimer::singleShot(0, this, &OverlayService::hideSnapAssist);
            return true;
        }
    }
    // Layout picker Escape is handled by a QML Shortcut in
    // LayoutPickerContent.qml that emits dismissRequested → hideLayoutPicker.
    // The C++ event-filter path was unreliable under keepMappedOnHide=true:
    // the Qt.WindowTransparentForInput flip during hide / show plus the
    // mutated keyboard_interactivity state left KeyPress delivery
    // intermittent on the warm path. Same pattern SnapAssistOverlay uses.
    return QObject::eventFilter(obj, event);
}

void OverlayService::hideSnapAssist()
{
    bool wasVisible = isSnapAssistVisible();
    const QString screenId = m_snapAssistScreenId;
    // Don't touch the SnapAssistThumbnailProvider cache here — entries are
    // keyed on KWin compositor handles (stable per-window for the window's
    // lifetime) and reused across continuation. Eviction is now automatic
    // via QCache LRU pressure; clearing on every dismiss would only force
    // an expensive recapture on the very next snap.
    //
    // Drop the per-show mutable copy though: it's a stale snapshot of the
    // last open's candidate list once the window is gone, and any
    // late-arriving setSnapAssistThumbnail call between hide and the next
    // show should land in the cache only — never mutate this list, since
    // it'd be replaced wholesale on the next showSnapAssist anyway.
    m_snapAssistCandidates.clear();
    // Hide rather than destroy. The QQuickWindow + QML scene survive
    // across user dismiss/re-show cycles so the next snap-assist trigger
    // reuses the warmed surface instead of paying QML compile + first-
    // paint pipeline build again. With keepMappedOnHide=false (default
    // for the snap-assist Surface), Surface::hide() unmaps the
    // wl_surface, which releases the layer-shell exclusive keyboard
    // grant while idle — no keyboard-grab footgun while the daemon is
    // sitting between snaps.
    if (m_snapAssistSurface) {
        m_snapAssistSurface->hide();
    }
    if (wasVisible) {
        Q_EMIT snapAssistDismissed();
    }
    // Re-show the zone selector for the specific virtual screen that was hidden in showSnapAssist
    // (symmetric: showSnapAssist only hides the selector for the target VS, not all VS).
    // Surface::show() pairs with the Surface::hide() in showSnapAssist so the
    // SurfaceAnimator drives the fade-in and the keepMappedOnHide flag flip is
    // reverted properly.
    //
    // Gate on Surface state: if a different code path (e.g. a fresh
    // showZoneSelector triggered by a new drag) already re-showed the
    // selector while snap-assist was visible, dispatching show() again would
    // cancel + replay the fade-in on an already-visible surface — visually a
    // "blip". Only re-show when the surface is genuinely Hidden.
    if (m_zoneSelectorVisible && !screenId.isEmpty()) {
        if (auto* selectorSurface = m_screenStates.value(screenId).zoneSelectorSurface) {
            if (!selectorSurface->isLogicallyShown()) {
                // cancelSurfacePrime: every other user-show path in this
                // service disarms the prime queue before show() so the
                // queued frameSwapped-driven hide doesn't race the user's
                // content off the screen. Missing it here would let a
                // selector that's still in its initial prime cycle (rare —
                // requires snap-assist to dismiss within the first painted
                // frame of the selector) flicker visible-then-hidden.
                cancelSurfacePrime(selectorSurface);
                selectorSurface->show();
            }
        }
    }
}

bool OverlayService::isSnapAssistVisible() const
{
    // Asymmetric with isLayoutPickerVisible (which reads Surface::state).
    // SnapAssist uses keepMappedOnHide=false (destroy-on-hide), so the
    // QQuickWindow isVisible flag IS the right "logically shown" signal —
    // the window is destroyed when it's logically hidden, and isVisible
    // tracks the live mapping state until then.
    // The picker uses keepMappedOnHide=true and stays Qt-visible across
    // logical hide cycles, so it must consult Surface state instead.
    // Both encode the same intent ("is this overlay logically on screen?"),
    // and both are correct for their respective lifecycle.
    return m_snapAssistWindow && m_snapAssistWindow->isVisible();
}

void OverlayService::createSnapAssistWindow(QScreen* physScreen)
{
    createSnapAssistWindowFor(physScreen, QRect(), QString());
}

void OverlayService::createSnapAssistWindowFor(QScreen* physScreen, const QRect& screenGeom, const QString& resolvedId)
{
    if (m_snapAssistSurface) {
        return;
    }

    QScreen* screen = physScreen ? physScreen : Utils::primaryScreen();
    if (!screen) {
        qCWarning(lcOverlay) << "createSnapAssistWindow: no screen";
        return;
    }

    // Virtual-screen anchors + margins (wlr-layer-shell attaches output+anchors
    // immutably, so they have to be right at create time).
    const auto placement = layerPlacementForVs(screenGeom, screen->geometry());
    std::optional<PhosphorLayer::Anchors> anchorsOverride(placement.anchors);
    std::optional<QMargins> marginsOverride;
    if (!placement.margins.isNull()) {
        marginsOverride = placement.margins;
    }

    const QString scopeId =
        resolvedId.isEmpty() ? Phosphor::Screens::ScreenIdentity::identifierFor(screen) : resolvedId;
    const auto role =
        PzRoles::makePerInstanceRole(PzRoles::SnapAssist, scopeId, m_surfaceManager->nextScopeGeneration());

    auto* surface = createLayerSurface({.qmlUrl = QUrl(QStringLiteral("qrc:/ui/SnapAssistOverlay.qml")),
                                        .screen = screen,
                                        .role = role,
                                        .windowType = "snap assist",
                                        .anchorsOverride = anchorsOverride,
                                        .marginsOverride = marginsOverride});
    if (!surface) {
        qCWarning(lcOverlay) << "Failed to create snap assist overlay";
        return;
    }

    m_snapAssistSurface = surface;
    m_snapAssistWindow = surface->window();
    m_snapAssistScreen = screen;

    connect(surface, &QObject::destroyed, this, [this, surf = surface]() {
        if (m_snapAssistSurface == surf) {
            m_snapAssistSurface = nullptr;
            m_snapAssistWindow = nullptr;
            m_snapAssistScreen = nullptr;
            m_snapAssistScreenId.clear();
        }
    });

    // Prime the render pipeline (see OverlayService::primeSurfaceRenderPipeline).
    // The user-show that follows immediately calls cancelSurfacePrime to
    // disarm the queued hide.
    primeSurfaceRenderPipeline(surface);

    // Emit snapAssistDismissed when the window is closed by QML (backdrop click, Escape)
    connect(m_snapAssistWindow, &QWindow::visibleChanged, this, [this](bool visible) {
        if (!visible) {
            Q_EMIT snapAssistDismissed();
        }
    });

    // windowSelected is declared in SnapAssistOverlay.qml; string-based
    // connect is the idiomatic way to reach QML-exposed signals since they
    // aren't addressable via Qt5-style &signal pointers.
    connect(m_snapAssistWindow, SIGNAL(windowSelected(QString, QString, QString)), this,
            SLOT(onSnapAssistWindowSelected(QString, QString, QString)));

    // Install event filter for reliable Escape key handling on Wayland.
    m_snapAssistWindow->installEventFilter(this);
    // Surface is in Hidden state (warmed) — caller calls show() after setting properties.
}

void OverlayService::warmUpSnapAssist()
{
    // Already warmed (or already-warm-from-a-real-user-show survived).
    if (m_snapAssistWindow) {
        return;
    }
    // Pick the primary screen at warm-up — snap-assist is single-instance
    // (one surface, recreated/relocated when a snap actually happens on a
    // different screen). The screen+rect on first user show takes
    // precedence over the warm screen via the existing reuseWindow path
    // in showSnapAssist (which destroys + recreates if the target screen
    // changed). For the typical single-monitor case the warm screen is
    // the eventual target screen and the user's first show is just a
    // setVisible toggle.
    QScreen* primary = QGuiApplication::primaryScreen();
    if (!primary) {
        qCWarning(lcOverlay) << "warmUpSnapAssist: no primary QScreen — skipping warm-up";
        return;
    }
    QRect screenGeom;
    if (m_screenManager) {
        const QString primaryId = Phosphor::Screens::ScreenIdentity::identifierFor(primary);
        screenGeom = m_screenManager->screenGeometry(primaryId);
    }
    if (!screenGeom.isValid()) {
        screenGeom = primary->geometry();
    }
    createSnapAssistWindowFor(primary, screenGeom, QString());
    qCInfo(lcOverlay) << "Pre-warmed SnapAssistOverlay for primary screen — first user-show will skip "
                         "QML compile + first-paint pipeline build";
}

void OverlayService::destroySnapAssistWindow()
{
    if (m_snapAssistSurface) {
        if (m_snapAssistWindow) {
            disconnect(m_snapAssistWindow, &QWindow::visibleChanged, this, nullptr);
            if (m_snapAssistScreen) {
                disconnect(m_snapAssistScreen, nullptr, m_snapAssistWindow, nullptr);
            }
        }
        m_snapAssistSurface->deleteLater();
        m_snapAssistSurface = nullptr;
        m_snapAssistWindow = nullptr;
    }
    m_snapAssistScreen = nullptr;
    m_snapAssistScreenId.clear();
}

void OverlayService::onSnapAssistWindowSelected(const QString& windowId, const QString& zoneId,
                                                const QString& geometryJson)
{
    // Keyboard interactivity stays Exclusive across selection: the window is
    // either dismissed by hideSnapAssist() (keyboard grab is then dropped
    // alongside the destroy) or reused by a continuation call to
    // showSnapAssist (which re-grabs explicitly). Releasing here would leak
    // the surface into a logically-shown-but-keyboard-unresponsive state if
    // any failure path drops the dismiss/continuation call — Escape and
    // Enter would silently no-op until the next show. The few-ms keyboard
    // capture during the D-Bus roundtrip to KWin is invisible at human
    // timescales; the failure-mode safety wins here.

    // Use the virtual-aware screen ID stored when snap assist was shown
    QString screenId = m_snapAssistScreenId;
    if (screenId.isEmpty() && m_snapAssistScreen) {
        screenId = Phosphor::Screens::ScreenIdentity::identifierFor(m_snapAssistScreen);
    }
    // geometryJson is overlay-local; daemon will fetch authoritative zone geometry from service
    Q_EMIT snapAssistWindowSelected(windowId, zoneId, geometryJson, screenId);
}

// ═══════════════════════════════════════════════════════════════════════════════
// PhosphorZones::Layout Picker Overlay
// ═══════════════════════════════════════════════════════════════════════════════

void OverlayService::showLayoutPicker(const QString& screenId)
{
    // Guard: if picker is currently shown (Surface state is Shown), don't
    // double-trigger. The surface stays Qt-visible across hide/show cycles
    // (keepMappedOnHide), so the Surface state machine is the right signal
    // for "logically visible" — not the QQuickWindow's isVisible().
    if (m_layoutPickerSurface && m_layoutPickerSurface->isLogicallyShown()) {
        return;
    }

    // Resolve target screen using shared helper (handles virtual IDs, fallback chain)
    QScreen* screen = resolveTargetScreen(m_screenManager, screenId);
    if (!screen) {
        qCWarning(lcOverlay) << "showLayoutPicker: no screen available";
        return;
    }

    // Use virtual screen geometry when available
    const QString resolvedId = screenId.isEmpty() ? Phosphor::Screens::ScreenIdentity::identifierFor(screen) : screenId;
    QRect screenGeom = resolveScreenGeometry(m_screenManager, resolvedId);
    if (!screenGeom.isValid()) {
        screenGeom = screen->geometry();
    }

    // Hide the zone selector for this specific virtual screen to avoid overlap.
    // Only hide the selector keyed by resolvedId, not all selectors on the physical monitor.
    // Route through Surface::hide() so the SurfaceAnimator drives the fade-out;
    // calling QQuickWindow::hide() directly would unmap the wl_surface and
    // discard the swapchain that keepMappedOnHide=true was meant to preserve.
    //
    // Reset the QML hover state BEFORE hiding (same rationale as showSnapAssist):
    // hideLayoutPicker re-shows the selector and autoScrollTimer would otherwise
    // tick on stale drag-time cursor coords.
    {
        const auto& selectorState = m_screenStates.value(resolvedId);
        if (selectorState.zoneSelectorSurface) {
            if (selectorState.zoneSelectorWindow) {
                QMetaObject::invokeMethod(selectorState.zoneSelectorWindow, "resetCursorState");
            }
            selectorState.zoneSelectorSurface->hide();
        }
    }

    // Always destroy + recreate. Reusing a still-mapped Surface across
    // hide/show would let a single keyboard_interactivity mutation be
    // silently ignored by KWin's wlr-layer-shell impl (focus is only
    // re-evaluated at initial map). A fresh per-show map gives Escape
    // / arrows / Enter focus reliably, at the cost of one
    // wl_surface attach + Vulkan swapchain init per open.
    destroyLayoutPickerWindow();
    createLayoutPickerWindowFor(screen, screenGeom, resolvedId);
    if (!m_layoutPickerWindow) {
        return;
    }

    m_layoutPickerScreen = screen;
    m_layoutPickerScreenId = resolvedId;

    // Build layouts list (use virtual-aware screen ID for correct layout
    // resolution). Pass the screen's available geometry as the autotile
    // preview canvas so algorithm thumbnails (BSP, fibonacci, …) split
    // along the same axis the live tiler will. Without this, on a
    // portrait VS the picker shows BSP as left/right while the tiler
    // actually places windows top/bottom.
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
        destroyLayoutPickerWindow();
        return;
    }

    // Determine active layout ID
    QString activeId;
    if (m_layoutManager) {
        PhosphorZones::Layout* activeLayout = resolveScreenLayout(resolvedId);
        if (activeLayout) {
            activeId = activeLayout->id().toString();
        }
    }

    // Calculate screen aspect ratio (use virtual screen geometry)
    qreal aspectRatio =
        (screenGeom.height() > 0) ? static_cast<qreal>(screenGeom.width()) / screenGeom.height() : (16.0 / 9.0);
    aspectRatio = qBound(0.5, aspectRatio, 4.0);

    // Set properties
    writeQmlProperty(m_layoutPickerWindow, QStringLiteral("layouts"), layoutsList);
    writeQmlProperty(m_layoutPickerWindow, QStringLiteral("activeLayoutId"), activeId);
    writeQmlProperty(m_layoutPickerWindow, QStringLiteral("screenAspectRatio"), aspectRatio);
    // Global "Auto-assign for all layouts" master toggle (#370) — see matching
    // comment in selector_update.cpp.
    writeQmlProperty(m_layoutPickerWindow, QStringLiteral("globalAutoAssign"),
                     m_settings && m_settings->autoAssignAllLayouts());
    writeFontProperties(m_layoutPickerWindow, m_settings);

    // Push lock state so picker disables non-active layout interaction
    // Check both modes — if either is locked for this context, show lock
    bool locked = false;
    if (m_settings && m_layoutManager) {
        int curDesktop = m_layoutManager->currentVirtualDesktop();
        QString curActivity = m_layoutManager->currentActivity();
        locked = isAnyModeLocked(m_settings, resolvedId, curDesktop, curActivity);
    }
    writeQmlProperty(m_layoutPickerWindow, QStringLiteral("locked"), locked);

    // Theme colors and zone appearance (consistent with zone selector)
    writeColorSettings(m_layoutPickerWindow, m_settings);

    // Anchors + margins were baked into the Surface by createLayoutPickerWindowFor above
    // using screenGeom, so positioning is already correct.
    //
    // assertWindowOnScreen + setWidth/setHeight are run on every show, even
    // on the reuse path — if the screen geometry changed between the warmed
    // surface's creation and now (e.g. external resolution change while the
    // picker was hidden), the cached dimensions would otherwise be stale.
    // Both calls are idempotent when nothing changed (Qt skips the property
    // notify when the new value matches the old), so the cost on the common
    // unchanged-geometry path is a few comparisons.
    assertWindowOnScreen(m_layoutPickerWindow, screen, screenGeom);
    m_layoutPickerWindow->setWidth(screenGeom.width());
    m_layoutPickerWindow->setHeight(screenGeom.height());

    // Activate the QML-side keyboard Shortcuts before show() so any
    // accelerator events that race the show animation reach a picker
    // that's expecting them.
    writeQmlProperty(m_layoutPickerWindow, QStringLiteral("_shortcutsActive"), true);
    // Reset the QML-side dismiss latch so multiple rapid backdrop
    // clicks during the show animation collapse to one dismiss.
    writeQmlProperty(m_layoutPickerWindow, QStringLiteral("_dismissed"), false);

    // The role's keyboard_interactivity (Exclusive — from
    // CenteredModal) is applied at the surface's initial map by the
    // LayerShellWindow ctor, so KWin grants keyboard focus on first
    // map. requestActivate() asks Qt to make this the focused
    // QWindow, which together with the layer-shell focus grant lands
    // input on the QQuickWindow so QML Shortcuts (Escape, arrows,
    // Enter) fire.
    // OSD-style content lifecycle — see ZoneSelectorWindow's `loaded`
    // property docstring. Toggle false→true so the LayoutPickerOverlay
    // Loader re-instantiates the popup body on every show.
    writeQmlProperty(m_layoutPickerWindow, QStringLiteral("loaded"), false);
    writeQmlProperty(m_layoutPickerWindow, QStringLiteral("loaded"), true);
    cancelSurfacePrime(m_layoutPickerSurface);
    m_layoutPickerSurface->show();
    m_layoutPickerWindow->requestActivate();

    qCInfo(lcOverlay) << "showLayoutPicker: screen=" << resolvedId << "layouts=" << layoutsList.size()
                      << "active=" << activeId;
}

void OverlayService::hideLayoutPicker()
{
    if (!m_layoutPickerSurface) {
        // Always emit dismissed so the daemon's Escape-shortcut release
        // path runs even on idempotent calls (defensive — multiple call
        // sites converge on hideLayoutPicker).
        Q_EMIT layoutPickerDismissed();
        return;
    }

    // Re-entrant call while the SurfaceAnimator's hide leg is still
    // running — let the timer scheduled below tear down the surface
    // when the animation finishes.
    if (m_layoutPickerHiding) {
        return;
    }
    m_layoutPickerHiding = true;

    // Capture screenId before the post-animation destroy clears
    // m_layoutPickerScreenId. surface is captured as QPointer so that
    // a re-show + dismiss cycle that destroys-and-recreates the picker
    // before the original 600ms timer fires doesn't leave a dangling
    // raw pointer in the lambda — the post-recreate `m_layoutPicker-
    // Surface == surface` check then reads freed memory.
    const QString screenId = m_layoutPickerScreenId;
    QPointer<PhosphorLayer::Surface> surface(m_layoutPickerSurface);

    // Drive the hide leg through SurfaceAnimator (configured at
    // applyShaderProfilesToAnimator → buildLayoutPickerConfig with
    // popup.layoutPicker.hide as the shader+motion paths).
    // Surface::hide() with keepMappedOnHide=true sets the window
    // transparent-for-input AND dispatches animator.beginHide() — the
    // animation runs on the still-mapped wl_surface so the user sees
    // the configured shader fade out.
    //
    // Surface::Impl transitions State to Hidden SYNCHRONOUSLY (see
    // libs/phosphor-layer/src/surface.cpp:360 — `transitionTo(Hidden)`
    // immediately after the no-op-callbacked beginHide). That means
    // Surface::stateChanged is NOT a usable signal for "animation
    // completed"; listening to it fires before any hide frame paints.
    // The animator's onComplete callback is captured inside the
    // library and not exposed.
    //
    // Pragmatic fix: schedule the wl_surface teardown via QTimer at
    // the configured hide duration. 600ms is comfortably above the
    // default popup.layoutPicker.hide profile (200ms). If a
    // user customises the profile beyond that, increase here too.
    constexpr int kDestroyDelayMs = 600;
    surface->hide();
    QTimer::singleShot(kDestroyDelayMs, this, [this, surface, screenId]() {
        m_layoutPickerHiding = false;

        // Supersession check: if showLayoutPicker ran during the 600 ms
        // window it called destroyLayoutPickerWindow + createLayoutPicker-
        // WindowFor, replacing m_layoutPickerSurface with a fresh address.
        // Any post-hide work below — destroying the captured surface,
        // restoring the selector, releasing the shared cancel-overlay
        // Escape registration — is wrong when a new picker is the live
        // one: the selector restore would target the OLD picker's
        // screenId (potentially the wrong monitor); the dismissed signal
        // would unregister Escape mid-legitimate-show, breaking the new
        // picker's keyboard cancel. Skip the whole body when supplanted.
        // m_layoutPickerSurface == nullptr means an external destroy
        // (screen lost, daemon shutdown) ran first — still emit dismissed
        // so listeners learn the picker is gone.
        const bool supplanted = m_layoutPickerSurface != nullptr && m_layoutPickerSurface != surface.data();
        if (supplanted) {
            return;
        }

        if (surface && m_layoutPickerSurface == surface.data()) {
            destroyLayoutPickerWindow();
        }

        if (m_zoneSelectorVisible && !screenId.isEmpty()) {
            if (auto* selectorSurface = m_screenStates.value(screenId).zoneSelectorSurface) {
                if (!selectorSurface->isLogicallyShown()) {
                    // cancelSurfacePrime: matches every other user-show
                    // path. Without it the selector's queued prime-hide
                    // (if still in flight on the very first show) races
                    // this user-driven re-show.
                    cancelSurfacePrime(selectorSurface);
                    selectorSurface->show();
                }
            }
        }

        // Release the shared cancel-overlay Escape registration.
        // cancelSnap() dismisses the picker when visible — see
        // WindowDragAdaptor::cancelSnap and
        // ensureCancelOverlayShortcutRegistered for the rationale.
        Q_EMIT layoutPickerDismissed();
    });
}

void OverlayService::warmUpLayoutPicker()
{
    // Intentional no-op. The picker used to be pre-warmed (a hidden
    // wl_surface created at daemon start so the first user-triggered
    // show skipped ~50-100 ms of Wayland + Vulkan + QML init), but that
    // mapped the surface with keyboard_interactivity=None to keep the
    // warm hidden surface from hijacking keys. KWin's wlr-layer-shell
    // doesn't re-evaluate keyboard focus when interactivity is mutated
    // to Exclusive on a still-mapped surface, so the picker never
    // received KeyPress events on user-show — Escape, arrow keys, and
    // Enter were all dead. Creating fresh per-show maps the surface
    // with Exclusive at initial map, which KWin honours.
    //
    // Kept as a stub rather than removed so the signals.cpp call site
    // and the public IOverlayService interface stay stable. Callers see
    // a nominal no-op; the perf cost is amortised across each open.
}

bool OverlayService::isLayoutPickerVisible() const
{
    // Phase 5 keepMappedOnHide: QQuickWindow.isVisible() stays true across
    // logical hide/show cycles, so it's not a signal of "currently shown"
    // anymore. Surface state machine is.
    return m_layoutPickerSurface && m_layoutPickerSurface->isLogicallyShown();
}

void OverlayService::createLayoutPickerWindow(QScreen* physScreen)
{
    createLayoutPickerWindowFor(physScreen, QRect(), QString());
}

void OverlayService::createLayoutPickerWindowFor(QScreen* physScreen, const QRect& screenGeom,
                                                 const QString& resolvedId)
{
    if (m_layoutPickerSurface) {
        return;
    }

    QScreen* screen = physScreen ? physScreen : Utils::primaryScreen();
    if (!screen) {
        return;
    }

    // Compute virtual-screen anchors/margins once, up front, since wlr-layer-
    // shell's anchors/output are immutable post-attach (v3; v4's mutable
    // anchors aren't exposed via PhosphorLayer yet). Physical screen → anchor
    // all four edges; virtual screen → anchor Top+Left with margin offset so
    // the window lands in the right region.
    const auto placement = layerPlacementForVs(screenGeom, screen->geometry());
    std::optional<PhosphorLayer::Anchors> anchorsOverride(placement.anchors);
    std::optional<QMargins> marginsOverride;
    if (!placement.margins.isNull()) {
        marginsOverride = placement.margins;
    }

    // Per-instance scope disambiguator so the compositor sees each open/close
    // cycle as a fresh surface (prevents configure-event rate-limiting on rapid
    // reopens).
    const QString scopeId =
        resolvedId.isEmpty() ? Phosphor::Screens::ScreenIdentity::identifierFor(screen) : resolvedId;
    const auto role =
        PzRoles::makePerInstanceRole(PzRoles::LayoutPicker, scopeId, m_surfaceManager->nextScopeGeneration());

    // Destroy on hide: each show maps a fresh wl_surface so KWin's
    // wlr-layer-shell evaluates keyboard_interactivity at "initial
    // map" and grants Exclusive focus — required for Escape and the
    // arrow-key navigation Shortcuts to fire. Keep-mapped variants
    // (kbd-toggle dance, kbd None→Exclusive mutation) were tried and
    // every one left Escape dead because KWin doesn't re-evaluate
    // focus on already-mapped surfaces.
    //
    // Trade-off: the QQuickWindow + QRhi tear down on every dismiss.
    // On the NVIDIA proprietary driver vkDestroyDevice can stall the
    // calling thread (devtalk 319139 / 195793 / 366254). Picker is
    // user-triggered and infrequent in typical use, so the stall is
    // amortised; SnapAssist uses the same pattern.
    //
    // keepMappedOnHide=true is REQUIRED here for the SurfaceAnimator's
    // hide leg (motion + shader) to actually paint. With the default
    // false, Surface::hide() calls beginHide and then immediately
    // hides the QQuickWindow — the animation frames never render
    // (libs/phosphor-layer/src/surface.cpp warns about this exact
    // mismatch). To preserve the destroy-on-hide invariant the
    // hideLayoutPicker handler listens for State::Hidden and tears
    // down the wl_surface on its own once the animation completes —
    // see overlayservice/snapassist.cpp::hideLayoutPicker.
    auto* surface = createLayerSurface({.qmlUrl = QUrl(QStringLiteral("qrc:/ui/LayoutPickerOverlay.qml")),
                                        .screen = screen,
                                        .role = role,
                                        .windowType = "layout picker",
                                        .anchorsOverride = anchorsOverride,
                                        .marginsOverride = marginsOverride,
                                        .keepMappedOnHide = true});
    if (!surface) {
        return;
    }

    auto* window = surface->window();

    connect(surface, &QObject::destroyed, this, [this, surf = surface]() {
        if (m_layoutPickerSurface == surf) {
            m_layoutPickerSurface = nullptr;
            m_layoutPickerWindow = nullptr;
            m_layoutPickerScreen = nullptr;
            m_layoutPickerScreenId.clear();
        }
    });

    // Prime the render pipeline so the user-show that follows races against
    // an already-mapped surface and a hot Vulkan swapchain — disarms the
    // shader's stale-FBO race. The user-show in showLayoutPicker will
    // cancelSurfacePrime before calling Surface::show() so the queued hide
    // doesn't fire on top of the user's content.
    primeSurfaceRenderPipeline(surface);

    // Connect layoutSelected and dismissRequested signals from QML.
    // dismissRequested() is the QML-visible "user dismissed" event — fired
    // from the backdrop MouseArea (and the C++ Escape event-filter path
    // routes through hideLayoutPicker directly, not via this signal). Same
    // signal name as LayoutOsd / NavigationOsd for consistency.
    connect(window, SIGNAL(layoutSelected(QString)), this, SLOT(onLayoutPickerSelected(QString)));
    connect(window, SIGNAL(dismissRequested()), this, SLOT(hideLayoutPicker()));

    // No visibleChanged → hide hookup here. The post-warmup design keeps the
    // Qt window `visible == true` for the surface's lifetime; the dismiss
    // mechanism is the dismissRequested signal above plus the library
    // animator flipping Qt::WindowTransparentForInput on the still-mapped
    // QWindow during hide. A visibleChanged hook would either never fire
    // (visible stays true under keepMappedOnHide=true) or, if a future
    // QML refactor flips visible explicitly, would re-enter destroy on
    // hide and reintroduce the slow path we're working around.

    // Escape is handled by the QML Shortcut in LayoutPickerContent.qml —
    // no QObject event filter needed (the eventFilter path was unreliable
    // under keepMappedOnHide=true; see eventFilter() comment).

    m_layoutPickerSurface = surface;
    m_layoutPickerWindow = window;
    // Surface is already warmed (hidden) — caller calls show() after setting properties.
}

void OverlayService::destroyLayoutPickerWindow()
{
    if (m_layoutPickerSurface) {
        if (m_layoutPickerWindow) {
            // No visibleChanged connection exists for the layout picker
            // window (unlike the snap-assist surface, which uses visibility
            // as a dismiss signal). Disconnect the screen-scoped signals we
            // do install, then let Surface's dtor handle the window.
            if (auto* screen = m_layoutPickerWindow->screen()) {
                disconnect(screen, nullptr, m_layoutPickerWindow, nullptr);
            }
        }
        m_layoutPickerSurface->deleteLater();
        m_layoutPickerSurface = nullptr;
        m_layoutPickerWindow = nullptr;
    }
    m_layoutPickerScreen = nullptr;
    m_layoutPickerScreenId.clear();
    // External destroy paths (screen lost, daemon teardown, layout
    // change) skip the hide animation entirely. Clear the hiding flag
    // here so a future show + hide cycle isn't stranded with the
    // re-entrancy guard latched.
    m_layoutPickerHiding = false;
}

void OverlayService::onLayoutPickerSelected(const QString& layoutId)
{
    qCInfo(lcOverlay) << "Layout picker selected=" << layoutId;
    hideLayoutPicker();
    Q_EMIT layoutPickerSelected(layoutId);
}

} // namespace PlasmaZones
