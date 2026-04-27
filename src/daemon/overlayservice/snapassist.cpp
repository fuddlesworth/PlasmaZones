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
#include "../windowthumbnailservice.h"
#include <QQuickWindow>
#include <QScreen>
#include <QQmlEngine>
#include <QKeyEvent>
#include <QTimer>

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

    // Reuse existing visible window when on the same screen (avoids QML compilation +
    // Wayland surface create/destroy churn during snap assist continuation).
    // Recreate when the target screen changes, window was closed, or doesn't exist.
    // Visibility check avoids re-showing a window whose Vulkan swapchain was torn
    // down by close() — only reuse windows that are still on-screen.
    const bool reuseWindow = m_snapAssistWindow && m_snapAssistWindow->isVisible() && m_snapAssistScreenId == screenId;
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

    // Start async thumbnail capture via KWin ScreenShot2. Overlay shows icons immediately.
    // Requires KWIN_SCREENSHOT_NO_PERMISSION_CHECKS=1 when desktop matching fails (local install).
    // Sequential capture (one at a time) to avoid overwhelming KWin; concurrent CaptureWindow
    // requests can cause thumbnails to stop working after the first few.
    if (!m_thumbnailService) {
        m_thumbnailService = std::make_unique<WindowThumbnailService>(this);
        connect(m_thumbnailService.get(), &WindowThumbnailService::captureFinished, this,
                [this](const QString& compositorHandle, const QString& dataUrl) {
                    updateSnapAssistCandidateThumbnail(compositorHandle, dataUrl);
                    processNextThumbnailCapture();
                });
    }
    // Apply cached thumbnails and queue only uncached ones (reuse across continuation)
    m_snapAssistCandidates.clear();
    m_thumbnailCaptureQueue.clear();
    if (m_thumbnailService->isAvailable()) {
        for (int i = 0; i < candidatesList.size(); ++i) {
            QVariantMap cand = candidatesList[i].toMap();
            QString compositorHandle = cand.value(QStringLiteral("compositorHandle")).toString();
            if (!compositorHandle.isEmpty()) {
                auto it = m_thumbnailCache.constFind(compositorHandle);
                if (it != m_thumbnailCache.constEnd() && !it.value().isEmpty()) {
                    cand[QStringLiteral("thumbnail")] = it.value();
                } else {
                    m_thumbnailCaptureQueue.append(compositorHandle);
                }
            }
            m_snapAssistCandidates.append(cand);
        }
        qCDebug(lcOverlay) << "showSnapAssist:" << m_thumbnailCache.size() << "cached,"
                           << m_thumbnailCaptureQueue.size() << "to capture";
        processNextThumbnailCapture();
    } else {
        m_snapAssistCandidates = candidatesList;
        qCDebug(lcOverlay) << "showSnapAssist: thumbnail service not available (auth?)";
    }

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
    if (m_snapAssistSurface) {
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

void OverlayService::setSnapAssistThumbnail(const QString& compositorHandle, const QString& dataUrl)
{
    updateSnapAssistCandidateThumbnail(compositorHandle, dataUrl);
}

void OverlayService::updateSnapAssistCandidateThumbnail(const QString& compositorHandle, const QString& dataUrl)
{
    if (dataUrl.isEmpty()) {
        return;
    }
    m_thumbnailCache.insert(compositorHandle, dataUrl);
    if (!m_snapAssistWindow || !m_snapAssistWindow->isVisible()) {
        return;
    }
    for (int i = 0; i < m_snapAssistCandidates.size(); ++i) {
        QVariantMap cand = m_snapAssistCandidates[i].toMap();
        if (cand.value(QStringLiteral("compositorHandle")).toString() == compositorHandle) {
            cand[QStringLiteral("thumbnail")] = dataUrl;
            m_snapAssistCandidates[i] = cand;
            writeQmlProperty(m_snapAssistWindow, QStringLiteral("candidates"), m_snapAssistCandidates);
            qCDebug(lcOverlay) << "SnapAssist: thumbnail updated for" << compositorHandle;
            break;
        }
    }
}

void OverlayService::processNextThumbnailCapture()
{
    if (!m_thumbnailService || m_thumbnailCaptureQueue.isEmpty()) {
        return;
    }
    const QString compositorHandle = m_thumbnailCaptureQueue.takeFirst();
    m_thumbnailService->captureWindowAsync(compositorHandle, 256);
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
    if (obj == m_layoutPickerWindow && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            QTimer::singleShot(0, this, &OverlayService::hideLayoutPicker);
            return true;
        }
    }
    return QObject::eventFilter(obj, event);
}

void OverlayService::hideSnapAssist()
{
    bool wasVisible = isSnapAssistVisible();
    const QString screenId = m_snapAssistScreenId;
    m_thumbnailCache.clear();
    destroySnapAssistWindow();
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
    const auto role = PzRoles::SnapAssist.withScopePrefix(
        QStringLiteral("plasmazones-snap-assist-%1-%2").arg(scopeId).arg(m_surfaceManager->nextScopeGeneration()));

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

    // Reuse the warmed surface when it's already attached to the right screen —
    // wlr-layer-shell v3 anchors are immutable post-attach, so a screen change
    // is the only thing that forces a destroy+recreate. On the same screen we
    // just update properties + invoke QML show() and skip the ~50-100 ms
    // Wayland-surface + Vulkan-swapchain init.
    const bool reuseSurface =
        m_layoutPickerSurface && m_layoutPickerScreen == screen && m_layoutPickerScreenId == resolvedId;
    if (!reuseSurface) {
        destroyLayoutPickerWindow();
        createLayoutPickerWindowFor(screen, screenGeom, resolvedId);
        if (!m_layoutPickerWindow) {
            return;
        }
    }

    m_layoutPickerScreen = screen;
    m_layoutPickerScreenId = resolvedId;

    // Build layouts list (use virtual-aware screen ID for correct layout resolution)
    QVariantList layoutsList = buildLayoutsList(resolvedId);
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

    // Re-grab keyboard focus on every show. Layer-shell keyboard
    // interactivity is mutable on the live wl_surface (snap-assist uses the
    // same setter pattern at line ~199). Without the regrab the surface
    // would still have `None` from the prior hide and would not receive
    // arrow / Enter / Escape input.
    if (m_layoutPickerSurface) {
        if (auto* handle = m_layoutPickerSurface->transport()) {
            handle->setKeyboardInteractivity(PhosphorLayer::KeyboardInteractivity::Exclusive);
        }
    }

    // Activate the QML-side keyboard Shortcuts before show() so any
    // accelerator events that race the show animation reach a picker
    // that's expecting them. Cleared in hideLayoutPicker so a logically-
    // hidden picker (still Qt-visible under keepMappedOnHide) doesn't
    // silently respond to stray accelerator deliveries.
    writeQmlProperty(m_layoutPickerWindow, QStringLiteral("_shortcutsActive"), true);

    // Phase 5: Surface::show() drives the SurfaceAnimator (registered for
    // PzRoles::LayoutPicker with osd.show + osd.pop) which animates
    // opacity 0→1 and scale 0.9→1 with overshoot. The library handles
    // visible=true and clears Qt.WindowTransparentForInput so input
    // routing returns. requestActivate() asks the compositor to focus
    // the still-Wayland-mapped layer surface (Wayland's keyboard focus
    // is otherwise gated by the keyboard_interactivity setter above).
    m_layoutPickerSurface->show();
    m_layoutPickerWindow->requestActivate();

    qCInfo(lcOverlay) << "showLayoutPicker: screen=" << resolvedId << "layouts=" << layoutsList.size()
                      << "active=" << activeId << "reuseSurface=" << reuseSurface;
}

void OverlayService::hideLayoutPicker()
{
    if (!m_layoutPickerSurface) {
        return;
    }

    // Disable the picker's QML-side keyboard Shortcuts before the surface
    // transitions to Hidden. Under keepMappedOnHide the QQuickWindow stays
    // Qt-visible, which means the Shortcuts remain registered with Qt's
    // accelerator pipeline; gating them on a property C++ controls keeps
    // a logically-hidden picker from responding to stray accelerator
    // events.
    if (m_layoutPickerWindow) {
        writeQmlProperty(m_layoutPickerWindow, QStringLiteral("_shortcutsActive"), false);
    }

    // Phase 5: Surface::hide() drives the SurfaceAnimator's beginHide
    // (osd.hide profile, opacity 1→0, scale 1→0.95). With keepMappedOnHide
    // the library does NOT call QQuickWindow::hide(); it flips
    // Qt.WindowTransparentForInput so the still-mapped layer surface stops
    // eating clicks. Re-entrancy is benign — Surface::hide is a no-op when
    // already in Hidden state.
    m_layoutPickerSurface->hide();

    // Drop keyboard interactivity so the still-mapped layer surface stops
    // intercepting keyboard events while the hide animation is in flight
    // and afterwards. Snap-assist doesn't need an analogous step — it
    // destroys on hide and the keyboard grab is dropped alongside the
    // wl_surface teardown in ~Surface. The picker stays Qt-mapped under
    // keepMappedOnHide=true, so we have to drop the grab explicitly.
    if (auto* handle = m_layoutPickerSurface->transport()) {
        handle->setKeyboardInteractivity(PhosphorLayer::KeyboardInteractivity::None);
    }

    // Re-show the zone selector that was hidden when layout picker was shown (line ~435).
    // Surface::show() pairs with the Surface::hide() in showLayoutPicker so the
    // SurfaceAnimator drives the fade-in and the keepMappedOnHide flag flip is
    // reverted properly. Same state-guard rationale as hideSnapAssist above —
    // skip the dispatch if the selector is already visible.
    const QString screenId = m_layoutPickerScreenId;
    if (m_zoneSelectorVisible && !screenId.isEmpty()) {
        if (auto* selectorSurface = m_screenStates.value(screenId).zoneSelectorSurface) {
            if (!selectorSurface->isLogicallyShown()) {
                selectorSurface->show();
            }
        }
    }
}

void OverlayService::warmUpLayoutPicker()
{
    if (m_layoutPickerSurface) {
        return;
    }
    // Pre-create the picker on the primary screen so the first user-triggered
    // show skips the ~50-100 ms Wayland surface + Vulkan swapchain init cost.
    // If the user later opens the picker on a different screen, showLayoutPicker
    // detects the screen mismatch and falls back to the destroy+recreate path —
    // the warm surface still saved one cold start.
    QScreen* screen = Utils::primaryScreen();
    if (!screen) {
        return;
    }
    const QString resolvedId = Phosphor::Screens::ScreenIdentity::identifierFor(screen);
    QRect screenGeom = resolveScreenGeometry(m_screenManager, resolvedId);
    if (!screenGeom.isValid()) {
        screenGeom = screen->geometry();
    }
    createLayoutPickerWindowFor(screen, screenGeom, resolvedId);
    if (m_layoutPickerSurface) {
        // Pre-warmed surface starts with no keyboard grab — show() will
        // promote to Exclusive when the user triggers the picker.
        if (auto* handle = m_layoutPickerSurface->transport()) {
            handle->setKeyboardInteractivity(PhosphorLayer::KeyboardInteractivity::None);
        }
        m_layoutPickerScreen = screen;
        m_layoutPickerScreenId = resolvedId;
        qCInfo(lcOverlay) << "Pre-warmed Layout Picker surface on screen=" << resolvedId;
    }
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
    const auto role = PzRoles::LayoutPicker.withScopePrefix(
        QStringLiteral("plasmazones-layout-picker-%1-%2").arg(scopeId).arg(m_surfaceManager->nextScopeGeneration()));

    // keepMappedOnHide=true: Phase 5 lifecycle. Surface stays Qt-visible
    // across hide/show cycles; SurfaceAnimator drives opacity for the
    // visual fade and the library flips Qt::WindowTransparentForInput
    // during the hide so the still-mapped layer surface stops eating
    // clicks.
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

    // Connect layoutSelected and dismissRequested signals from QML.
    // dismissRequested() is the QML-visible "user dismissed" event — fired
    // from the backdrop MouseArea (and the C++ Escape event-filter path
    // routes through hideLayoutPicker directly, not via this signal). Same
    // signal name as LayoutOsd / NavigationOsd for consistency.
    connect(window, SIGNAL(layoutSelected(QString)), this, SLOT(onLayoutPickerSelected(QString)));
    connect(window, SIGNAL(dismissRequested()), this, SLOT(hideLayoutPicker()));

    // No visibleChanged → hide hookup here. The post-warmup design keeps the
    // Qt window `visible == true` for the surface's lifetime; the dismiss
    // mechanism is the QML `_pickerDismissed` flag flipping the
    // WindowTransparentForInput bit. A visibleChanged hook would either
    // never fire (visible stays true) or, if a future QML refactor flips
    // visible explicitly, would re-enter destroy on hide and reintroduce
    // the slow path we're working around.

    // Install event filter for reliable Escape key handling on Wayland
    window->installEventFilter(this);

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
}

void OverlayService::onLayoutPickerSelected(const QString& layoutId)
{
    qCInfo(lcOverlay) << "Layout picker selected=" << layoutId;
    hideLayoutPicker();
    Q_EMIT layoutPickerSelected(layoutId);
}

} // namespace PlasmaZones
