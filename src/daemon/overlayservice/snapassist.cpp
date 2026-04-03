// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "internal.h"
#include "../overlayservice.h"
#include "../../core/logging.h"
#include "../../core/layout.h"
#include "../../core/layoututils.h"
#include "../../core/utils.h"
#include "../../core/screenmanager.h"
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

    QScreen* screen = resolveTargetScreen(screenId);
    if (!screen) {
        qCWarning(lcOverlay) << "showSnapAssist: no screen available";
        Q_EMIT snapAssistDismissed();
        return;
    }

    // Always destroy and recreate to avoid stale QML state (zone sizes wrong after continuation)
    destroySnapAssistWindow();
    createSnapAssistWindow();
    if (!m_snapAssistWindow) {
        Q_EMIT snapAssistDismissed();
        return;
    }

    m_snapAssistScreen = screen;

    // Parse JSON using shared helper (same format: array of objects)
    const QVariantList zonesList = parseZonesJson(emptyZonesJson, "showSnapAssist:");
    QVariantList candidatesList = parseZonesJson(candidatesJson, "showSnapAssist:");

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
    writeQmlProperty(m_snapAssistWindow, QStringLiteral("screenWidth"), screen->geometry().width());
    writeQmlProperty(m_snapAssistWindow, QStringLiteral("screenHeight"), screen->geometry().height());

    // Zone appearance defaults (used when zone.useCustomColors is false) - match main overlay
    if (m_settings) {
        writeQmlProperty(m_snapAssistWindow, QStringLiteral("highlightColor"), m_settings->highlightColor());
        writeQmlProperty(m_snapAssistWindow, QStringLiteral("inactiveColor"), m_settings->inactiveColor());
        writeQmlProperty(m_snapAssistWindow, QStringLiteral("borderColor"), m_settings->borderColor());
        writeQmlProperty(m_snapAssistWindow, QStringLiteral("activeOpacity"), m_settings->activeOpacity());
        writeQmlProperty(m_snapAssistWindow, QStringLiteral("inactiveOpacity"), m_settings->inactiveOpacity());
        writeQmlProperty(m_snapAssistWindow, QStringLiteral("borderWidth"), m_settings->borderWidth());
        writeQmlProperty(m_snapAssistWindow, QStringLiteral("borderRadius"), m_settings->borderRadius());
    }

    // Match main overlay: full-screen anchors so zone coordinates (overlay-local) line up
    if (!configureLayerSurface(m_snapAssistWindow, screen, LayerSurface::LayerTop,
                               LayerSurface::KeyboardInteractivityExclusive,
                               QStringLiteral("plasmazones-snap-assist-%1-%2")
                                   .arg(Utils::screenIdentifier(screen))
                                   .arg(++m_scopeGeneration),
                               LayerSurface::AnchorAll)) {
        qCWarning(lcOverlay) << "showSnapAssist: failed to configure layer surface";
        destroySnapAssistWindow();
        Q_EMIT snapAssistDismissed();
        return;
    }

    assertWindowOnScreen(m_snapAssistWindow, screen);
    // Size only — position is controlled by layer-surface anchors (AnchorAll),
    // setX/setY are no-ops on layer surfaces.
    const QRect snapGeom = screen->geometry();
    m_snapAssistWindow->setWidth(snapGeom.width());
    m_snapAssistWindow->setHeight(snapGeom.height());
    m_snapAssistWindow->show();
    // Ensure the window receives keyboard focus for Escape handling on Wayland.
    // KeyboardInteractivityExclusive tells the compositor to send keyboard events,
    // but Qt may not set internal focus without an explicit activation request.
    m_snapAssistWindow->requestActivate();
    qCInfo(lcOverlay) << "showSnapAssist: screen=" << screenId << "zones=" << zonesList.size()
                      << "candidates=" << candidatesList.size();

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
    m_thumbnailCache.clear();
    destroySnapAssistWindow();
    if (wasVisible) {
        Q_EMIT snapAssistDismissed();
    }
}

bool OverlayService::isSnapAssistVisible() const
{
    return m_snapAssistWindow && m_snapAssistWindow->isVisible();
}

void OverlayService::createSnapAssistWindow()
{
    if (m_snapAssistWindow) {
        return;
    }

    QScreen* screen = Utils::primaryScreen();
    if (!screen) {
        qCWarning(lcOverlay) << "createSnapAssistWindow: no screen";
        return;
    }

    auto* window = createQmlWindow(QUrl(QStringLiteral("qrc:/ui/SnapAssistOverlay.qml")), screen, "snap assist");
    if (!window) {
        qCWarning(lcOverlay) << "Failed to create snap assist overlay";
        return;
    }

    connect(window, &QObject::destroyed, this, [this]() {
        m_snapAssistWindow = nullptr;
        m_snapAssistScreen = nullptr;
    });

    // Emit snapAssistDismissed when the window is closed by QML (user selection, backdrop click, Escape)
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

    m_snapAssistWindow = window;
    m_snapAssistScreen = screen;
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
}

void OverlayService::onSnapAssistWindowSelected(const QString& windowId, const QString& zoneId,
                                                const QString& geometryJson)
{
    // Use stable EDID-based screen ID so the daemon's layout/tracking system
    // resolves the correct screen without relying on connector-name fallbacks.
    QString screenId = m_snapAssistScreen ? Utils::screenIdentifier(m_snapAssistScreen) : QString();
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

    // Resolve target screen
    QScreen* screen = resolveTargetScreen(screenId);
    if (!screen) {
        qCWarning(lcOverlay) << "showLayoutPicker: no screen available";
        return;
    }

    // Always destroy and recreate for fresh state
    destroyLayoutPickerWindow();
    createLayoutPickerWindow(screen);
    if (!m_layoutPickerWindow) {
        return;
    }

    // Build layouts list
    const QString resolvedScreenId = Utils::screenIdentifier(screen);
    QVariantList layoutsList = buildLayoutsList(resolvedScreenId);
    if (layoutsList.isEmpty()) {
        qCDebug(lcOverlay) << "showLayoutPicker: no layouts available";
        destroyLayoutPickerWindow();
        return;
    }

    // Determine active layout ID
    QString activeId;
    if (m_layoutManager) {
        Layout* activeLayout = resolveScreenLayout(screen);
        if (activeLayout) {
            activeId = activeLayout->id().toString();
        }
    }

    // Calculate screen aspect ratio
    const QRect screenGeom = screen->geometry();
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
        locked =
            m_settings->isContextLocked(QStringLiteral("0:") + Utils::screenIdentifier(screen), curDesktop, curActivity)
            || m_settings->isContextLocked(QStringLiteral("1:") + Utils::screenIdentifier(screen), curDesktop,
                                           curActivity);
    }
    writeQmlProperty(m_layoutPickerWindow, QStringLiteral("locked"), locked);

    // Theme colors and zone appearance (consistent with zone selector)
    if (m_settings) {
        writeQmlProperty(m_layoutPickerWindow, QStringLiteral("highlightColor"), m_settings->highlightColor());
        writeQmlProperty(m_layoutPickerWindow, QStringLiteral("inactiveColor"), m_settings->inactiveColor());
        writeQmlProperty(m_layoutPickerWindow, QStringLiteral("borderColor"), m_settings->borderColor());
        writeQmlProperty(m_layoutPickerWindow, QStringLiteral("activeOpacity"), m_settings->activeOpacity());
        writeQmlProperty(m_layoutPickerWindow, QStringLiteral("inactiveOpacity"), m_settings->inactiveOpacity());
    }

    // Full-screen layer shell with keyboard interactivity
    if (!configureLayerSurface(m_layoutPickerWindow, screen, LayerSurface::LayerTop,
                               LayerSurface::KeyboardInteractivityExclusive,
                               QStringLiteral("plasmazones-layout-picker-%1-%2")
                                   .arg(Utils::screenIdentifier(screen))
                                   .arg(++m_scopeGeneration),
                               LayerSurface::AnchorAll)) {
        qCWarning(lcOverlay) << "showLayoutPicker: failed to configure layer surface";
        destroyLayoutPickerWindow();
        return;
    }

    assertWindowOnScreen(m_layoutPickerWindow, screen);
    // Size only — position is controlled by layer-surface anchors (AnchorAll),
    // setX/setY are no-ops on layer surfaces.
    m_layoutPickerWindow->setWidth(screenGeom.width());
    m_layoutPickerWindow->setHeight(screenGeom.height());
    QMetaObject::invokeMethod(m_layoutPickerWindow, "show");
    m_layoutPickerWindow->requestActivate();

    qCInfo(lcOverlay) << "showLayoutPicker: screen=" << screen->name() << "layouts=" << layoutsList.size()
                      << "active=" << activeId;
}

void OverlayService::hideLayoutPicker()
{
    destroyLayoutPickerWindow();
}

bool OverlayService::isLayoutPickerVisible() const
{
    return m_layoutPickerWindow && m_layoutPickerWindow->isVisible();
}

void OverlayService::createLayoutPickerWindow(QScreen* screen)
{
    if (m_layoutPickerWindow) {
        return;
    }

    auto* window = createQmlWindow(QUrl(QStringLiteral("qrc:/ui/LayoutPickerOverlay.qml")), screen, "layout picker");
    if (!window) {
        qCWarning(lcOverlay) << "Failed to create layout picker overlay";
        return;
    }

    connect(window, &QObject::destroyed, this, [this]() {
        m_layoutPickerWindow = nullptr;
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
}

void OverlayService::onLayoutPickerSelected(const QString& layoutId)
{
    qCInfo(lcOverlay) << "Layout picker selected=" << layoutId;
    hideLayoutPicker();
    Q_EMIT layoutPickerSelected(layoutId);
}

} // namespace PlasmaZones
