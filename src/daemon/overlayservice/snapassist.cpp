// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "internal.h"
#include "../overlayservice.h"
#include "../../core/logging.h"
#include "../../core/layout.h"
#include "../../core/layoututils.h"
#include "../../core/utils.h"
#include "../../core/screenmanager.h"
#include "../../core/virtualscreen.h"
#include "../windowthumbnailservice.h"
#include <QQuickWindow>
#include <QScreen>
#include <QQmlEngine>
#include <QKeyEvent>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include "../../core/layersurface.h"

namespace PlasmaZones {

// parseZonesJson is defined in overlayservice_internal.h (shared inline)

void OverlayService::showSnapAssist(const QString& screenId, const QString& emptyZonesJson,
                                    const QString& candidatesJson)
{
    if (emptyZonesJson.isEmpty() || candidatesJson.isEmpty()) {
        qCDebug(lcOverlay) << "showSnapAssist: no empty zones or candidates";
        Q_EMIT snapAssistDismissed(); // Notify listeners that snap assist won't show
        return;
    }

    // Resolve physical screen using shared helper (handles virtual IDs, fallback chain)
    QScreen* screen = resolveTargetScreen(screenId);
    if (!screen) {
        qCWarning(lcOverlay) << "showSnapAssist: no screen available";
        Q_EMIT snapAssistDismissed();
        return;
    }

    // Parse JSON early — needed for both stale-request check and QML property push.
    const QVariantList zonesList = parseZonesJson(emptyZonesJson, "showSnapAssist:");
    QVariantList candidatesList = parseZonesJson(candidatesJson, "showSnapAssist:");

    // Guard against stale snap assist requests from a previous layout.
    // The KWin effect computes empty zones asynchronously; by the time the D-Bus
    // request arrives, the layout may have been switched and the zone IDs are no
    // longer valid. Verify that at least one requested zone exists in the current
    // layout for the target screen.
    Layout* currentLayout = resolveScreenLayout(screenId);
    if (currentLayout) {
        bool anyValid = false;
        for (const QVariant& z : zonesList) {
            const QString zoneId = z.toMap().value(QLatin1String("zoneId")).toString();
            if (!zoneId.isEmpty() && currentLayout->zoneById(QUuid::fromString(zoneId))) {
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
    QRect screenGeom = resolveScreenGeometry(screenId);
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
        createSnapAssistWindow(screen);
        if (!m_snapAssistWindow) {
            Q_EMIT snapAssistDismissed();
            return;
        }
    }

    m_snapAssistScreen = screen;
    m_snapAssistScreenId = screenId;

    // Hide zone selectors for ALL virtual screens on the same physical monitor,
    // since the snap assist window covers the entire physical screen area.
    const QString physId =
        VirtualScreenId::isVirtual(screenId) ? VirtualScreenId::extractPhysicalId(screenId) : screenId;
    auto* mgr = ScreenManager::instance();
    if (mgr && mgr->hasVirtualScreens(physId)) {
        for (const QString& vsId : mgr->virtualScreenIdsFor(physId)) {
            if (auto* selectorWindow = m_screenStates.value(vsId).zoneSelectorWindow) {
                selectorWindow->hide();
            }
        }
    } else {
        if (auto* selectorWindow = m_screenStates.value(screenId).zoneSelectorWindow) {
            selectorWindow->hide();
        }
    }

    // Start async thumbnail capture via KWin ScreenShot2. Overlay shows icons immediately.
    // Requires KWIN_SCREENSHOT_NO_PERMISSION_CHECKS=1 when desktop matching fails (local install).
    // Sequential capture (one at a time) to avoid overwhelming KWin; concurrent CaptureWindow
    // requests can cause thumbnails to stop working after the first few.
    if (!m_thumbnailService) {
        m_thumbnailService = std::make_unique<WindowThumbnailService>(this);
        connect(m_thumbnailService.get(), &WindowThumbnailService::captureFinished, this,
                [this](const QString& kwinHandle, const QString& dataUrl) {
                    updateSnapAssistCandidateThumbnail(kwinHandle, dataUrl);
                    processNextThumbnailCapture();
                });
    }
    // Apply cached thumbnails and queue only uncached ones (reuse across continuation)
    m_snapAssistCandidates.clear();
    m_thumbnailCaptureQueue.clear();
    if (m_thumbnailService->isAvailable()) {
        for (int i = 0; i < candidatesList.size(); ++i) {
            QVariantMap cand = candidatesList[i].toMap();
            QString kwinHandle = cand.value(QStringLiteral("kwinHandle")).toString();
            if (!kwinHandle.isEmpty()) {
                auto it = m_thumbnailCache.constFind(kwinHandle);
                if (it != m_thumbnailCache.constEnd() && !it.value().isEmpty()) {
                    cand[QStringLiteral("thumbnail")] = it.value();
                } else {
                    m_thumbnailCaptureQueue.append(kwinHandle);
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

    // Zone appearance defaults (used when zone.useCustomColors is false) - match main overlay
    writeColorSettings(m_snapAssistWindow, m_settings);
    if (m_settings) {
        writeQmlProperty(m_snapAssistWindow, QStringLiteral("borderWidth"), m_settings->borderWidth());
        writeQmlProperty(m_snapAssistWindow, QStringLiteral("borderRadius"), m_settings->borderRadius());
    }

    // Configure layer surface only on fresh creation (not reuse) — reconfiguring
    // an already-visible layer surface with a new scope is unnecessary and can
    // confuse KWin's rate-limiting. Size/screen are already correct for reuse.
    if (reuseWindow) {
        // Restore exclusive keyboard grab for Escape handling — it was released
        // in onSnapAssistWindowSelected() to keep the desktop responsive during
        // the D-Bus roundtrip.
        if (auto* ls = LayerSurface::find(m_snapAssistWindow)) {
            ls->setKeyboardInteractivity(LayerSurface::KeyboardInteractivityExclusive);
        }
    } else {
        if (!configureLayerSurface(
                m_snapAssistWindow, screen, LayerSurface::LayerTop, LayerSurface::KeyboardInteractivityExclusive,
                QStringLiteral("plasmazones-snap-assist-%1-%2").arg(screenId).arg(++m_scopeGeneration))) {
            qCWarning(lcOverlay) << "showSnapAssist: failed to configure layer surface";
            destroySnapAssistWindow();
            Q_EMIT snapAssistDismissed();
            return;
        }
        // Apply virtual screen positioning (anchors + margins) after configureLayerSurface
        updateWindowScreenPosition(m_snapAssistWindow, screenId);
    }

    if (!reuseWindow) {
        assertWindowOnScreen(m_snapAssistWindow, screen, screenGeom);
        m_snapAssistWindow->setWidth(screenGeom.width());
        m_snapAssistWindow->setHeight(screenGeom.height());
    }
    m_snapAssistWindow->show();
    // Ensure the window receives keyboard focus for Escape handling on Wayland.
    // KeyboardInteractivityExclusive tells the compositor to send keyboard events,
    // but Qt may not set internal focus without an explicit activation request.
    m_snapAssistWindow->requestActivate();
    qCInfo(lcOverlay) << "showSnapAssist: screen=" << screenId << "zones=" << zonesList.size()
                      << "candidates=" << candidatesList.size() << "reuse=" << reuseWindow;

    Q_EMIT snapAssistShown(screenId, emptyZonesJson, candidatesJson);
}

void OverlayService::setSnapAssistThumbnail(const QString& kwinHandle, const QString& dataUrl)
{
    updateSnapAssistCandidateThumbnail(kwinHandle, dataUrl);
}

void OverlayService::updateSnapAssistCandidateThumbnail(const QString& kwinHandle, const QString& dataUrl)
{
    if (dataUrl.isEmpty()) {
        return;
    }
    m_thumbnailCache.insert(kwinHandle, dataUrl);
    if (!m_snapAssistWindow || !m_snapAssistWindow->isVisible()) {
        return;
    }
    for (int i = 0; i < m_snapAssistCandidates.size(); ++i) {
        QVariantMap cand = m_snapAssistCandidates[i].toMap();
        if (cand.value(QStringLiteral("kwinHandle")).toString() == kwinHandle) {
            cand[QStringLiteral("thumbnail")] = dataUrl;
            m_snapAssistCandidates[i] = cand;
            writeQmlProperty(m_snapAssistWindow, QStringLiteral("candidates"), m_snapAssistCandidates);
            qCDebug(lcOverlay) << "SnapAssist: thumbnail updated for" << kwinHandle;
            break;
        }
    }
}

void OverlayService::processNextThumbnailCapture()
{
    if (!m_thumbnailService || m_thumbnailCaptureQueue.isEmpty()) {
        return;
    }
    const QString kwinHandle = m_thumbnailCaptureQueue.takeFirst();
    m_thumbnailService->captureWindowAsync(kwinHandle, 256);
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
    // Re-show zone selectors for ALL virtual screens on the same physical monitor
    // (symmetric with the hide in showSnapAssist).
    if (m_zoneSelectorVisible && !screenId.isEmpty()) {
        const QString physId =
            VirtualScreenId::isVirtual(screenId) ? VirtualScreenId::extractPhysicalId(screenId) : screenId;
        auto* mgr = ScreenManager::instance();
        if (mgr && mgr->hasVirtualScreens(physId)) {
            for (const QString& vsId : mgr->virtualScreenIdsFor(physId)) {
                if (auto* selectorWindow = m_screenStates.value(vsId).zoneSelectorWindow) {
                    selectorWindow->show();
                }
            }
        } else {
            if (auto* selectorWindow = m_screenStates.value(screenId).zoneSelectorWindow) {
                selectorWindow->show();
            }
        }
    }
}

bool OverlayService::isSnapAssistVisible() const
{
    return m_snapAssistWindow && m_snapAssistWindow->isVisible();
}

void OverlayService::createSnapAssistWindow(QScreen* physScreen)
{
    if (m_snapAssistWindow) {
        return;
    }

    QScreen* screen = physScreen ? physScreen : Utils::primaryScreen();
    if (!screen) {
        qCWarning(lcOverlay) << "createSnapAssistWindow: no screen";
        return;
    }

    auto* window = createQmlWindow(QUrl(QStringLiteral("qrc:/ui/SnapAssistOverlay.qml")), screen, "snap assist");
    if (!window) {
        qCWarning(lcOverlay) << "Failed to create snap assist overlay";
        return;
    }

    m_snapAssistWindow = window;
    m_snapAssistScreen = screen;

    connect(window, &QObject::destroyed, this, [this, win = window]() {
        if (m_snapAssistWindow == win) {
            m_snapAssistWindow = nullptr;
            m_snapAssistScreen = nullptr;
            m_snapAssistScreenId.clear();
        }
    });

    // Emit snapAssistDismissed when the window is closed by QML (backdrop click, Escape)
    connect(window, &QWindow::visibleChanged, this, [this](bool visible) {
        if (!visible) {
            Q_EMIT snapAssistDismissed();
        }
    });

    // Connect windowSelected from QML: convert overlay-local geometry to screen
    // coordinates before emitting (KWin effect needs global coordinates for moveResize)
    connect(window, SIGNAL(windowSelected(QString, QString, QString)), this,
            SLOT(onSnapAssistWindowSelected(QString, QString, QString)));

    // Install event filter for reliable Escape key handling on Wayland.
    // The QML Shortcut may not fire if the layer shell keyboard focus
    // isn't fully reflected in Qt's internal focus model.
    window->installEventFilter(this);
    window->setVisible(false);
}

void OverlayService::destroySnapAssistWindow()
{
    if (m_snapAssistWindow) {
        // Disconnect visibleChanged before closing to prevent spurious snapAssistDismissed
        // when the window is being destroyed and recreated (e.g. showSnapAssist recreate cycle)
        disconnect(m_snapAssistWindow, &QWindow::visibleChanged, this, nullptr);
        // Disconnect screen signals so no geometryChanged etc. are delivered during teardown
        if (m_snapAssistScreen) {
            disconnect(m_snapAssistScreen, nullptr, m_snapAssistWindow, nullptr);
        }
        m_snapAssistWindow->close();
        m_snapAssistWindow->destroy();
        m_snapAssistWindow->deleteLater();
        m_snapAssistWindow = nullptr;
    }
    m_snapAssistScreen = nullptr;
    m_snapAssistScreenId.clear();
}

void OverlayService::onSnapAssistWindowSelected(const QString& windowId, const QString& zoneId,
                                                const QString& geometryJson)
{
    // Release exclusive keyboard grab immediately so the desktop remains
    // responsive while the D-Bus roundtrip to KWin processes the snap.
    // The window stays visible for potential reuse by showSnapAssist() continuation.
    if (m_snapAssistWindow) {
        if (auto* ls = LayerSurface::find(m_snapAssistWindow)) {
            ls->setKeyboardInteractivity(LayerSurface::KeyboardInteractivityNone);
        }
    }

    // Use the virtual-aware screen ID stored when snap assist was shown
    QString screenId = m_snapAssistScreenId;
    if (screenId.isEmpty() && m_snapAssistScreen) {
        screenId = Utils::screenIdentifier(m_snapAssistScreen);
    }
    // geometryJson is overlay-local; daemon will fetch authoritative zone geometry from service
    Q_EMIT snapAssistWindowSelected(windowId, zoneId, geometryJson, screenId);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Layout Picker Overlay
// ═══════════════════════════════════════════════════════════════════════════════

void OverlayService::showLayoutPicker(const QString& screenId)
{
    // Guard: if picker window already exists (visible or being set up), do nothing.
    // Prevents double-trigger when shortcut fires before KeyboardInteractivityExclusive
    // grabs the keyboard on Wayland, and avoids deleteLater() races with stale grabs.
    if (m_layoutPickerWindow) {
        return;
    }

    // Resolve target screen using shared helper (handles virtual IDs, fallback chain)
    QScreen* screen = resolveTargetScreen(screenId);
    if (!screen) {
        qCWarning(lcOverlay) << "showLayoutPicker: no screen available";
        return;
    }

    // Use virtual screen geometry when available
    const QString resolvedId = screenId.isEmpty() ? Utils::screenIdentifier(screen) : screenId;
    QRect screenGeom = resolveScreenGeometry(resolvedId);
    if (!screenGeom.isValid()) {
        screenGeom = screen->geometry();
    }

    // Hide the zone selector for this specific virtual screen to avoid overlap.
    // Only hide the selector keyed by resolvedId, not all selectors on the physical monitor.
    if (auto* selectorWindow = m_screenStates.value(resolvedId).zoneSelectorWindow) {
        selectorWindow->hide();
    }

    // Always destroy and recreate for fresh state
    destroyLayoutPickerWindow();
    createLayoutPickerWindow(screen);
    if (!m_layoutPickerWindow) {
        return;
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
        Layout* activeLayout = resolveScreenLayout(resolvedId);
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

    // Layer shell with keyboard interactivity — position to virtual or physical screen
    if (!configureLayerSurface(
            m_layoutPickerWindow, screen, LayerSurface::LayerTop, LayerSurface::KeyboardInteractivityExclusive,
            QStringLiteral("plasmazones-layout-picker-%1-%2").arg(resolvedId).arg(++m_scopeGeneration))) {
        qCWarning(lcOverlay) << "showLayoutPicker: failed to configure layer surface";
        destroyLayoutPickerWindow();
        return;
    }
    // Apply virtual screen positioning (anchors + margins) after configureLayerSurface
    updateWindowScreenPosition(m_layoutPickerWindow, resolvedId);

    assertWindowOnScreen(m_layoutPickerWindow, screen, screenGeom);
    // Size only — position is controlled by layer-surface anchors + margins,
    // setX/setY are no-ops on layer surfaces.
    m_layoutPickerWindow->setWidth(screenGeom.width());
    m_layoutPickerWindow->setHeight(screenGeom.height());
    QMetaObject::invokeMethod(m_layoutPickerWindow, "show");
    m_layoutPickerWindow->requestActivate();

    qCInfo(lcOverlay) << "showLayoutPicker: screen=" << resolvedId << "layouts=" << layoutsList.size()
                      << "active=" << activeId;
}

void OverlayService::hideLayoutPicker()
{
    const QString screenId = m_layoutPickerScreenId;
    destroyLayoutPickerWindow();
    // Re-show the zone selector that was hidden when layout picker was shown (line 322-324)
    if (m_zoneSelectorVisible && !screenId.isEmpty()) {
        if (auto* selectorWindow = m_screenStates.value(screenId).zoneSelectorWindow) {
            selectorWindow->show();
        }
    }
}

bool OverlayService::isLayoutPickerVisible() const
{
    return m_layoutPickerWindow && m_layoutPickerWindow->isVisible();
}

void OverlayService::createLayoutPickerWindow(QScreen* physScreen)
{
    if (m_layoutPickerWindow) {
        return;
    }

    QScreen* screen = physScreen ? physScreen : Utils::primaryScreen();
    auto* window = createQmlWindow(QUrl(QStringLiteral("qrc:/ui/LayoutPickerOverlay.qml")), screen, "layout picker");
    if (!window) {
        qCWarning(lcOverlay) << "Failed to create layout picker overlay";
        return;
    }

    connect(window, &QObject::destroyed, this, [this, win = window]() {
        if (m_layoutPickerWindow == win) {
            m_layoutPickerWindow = nullptr;
            m_layoutPickerScreen = nullptr;
            m_layoutPickerScreenId.clear();
        }
    });

    // Connect layoutSelected and dismissed signals from QML
    connect(window, SIGNAL(layoutSelected(QString)), this, SLOT(onLayoutPickerSelected(QString)));
    connect(window, SIGNAL(dismissed()), this, SLOT(hideLayoutPicker()));

    // Install event filter for reliable Escape key handling on Wayland
    window->installEventFilter(this);

    m_layoutPickerWindow = window;
    window->setVisible(false);
}

void OverlayService::destroyLayoutPickerWindow()
{
    if (m_layoutPickerWindow) {
        disconnect(m_layoutPickerWindow, &QWindow::visibleChanged, this, nullptr);
        // Disconnect screen signals so no geometryChanged etc. are delivered during teardown
        if (auto* screen = m_layoutPickerWindow->screen()) {
            disconnect(screen, nullptr, m_layoutPickerWindow, nullptr);
        }
        m_layoutPickerWindow->close();
        m_layoutPickerWindow->destroy();
        m_layoutPickerWindow->deleteLater();
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
