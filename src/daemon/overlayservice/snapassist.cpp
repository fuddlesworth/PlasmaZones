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
#include <PhosphorShell/LayerSurface.h>

#include <PhosphorLayer/Surface.h>
#include <PhosphorLayer/ILayerShellTransport.h>
#include "pz_roles.h"
using PhosphorShell::LayerSurface;
namespace LayerSurfaceProps = PhosphorShell::LayerSurfaceProps;

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
    QScreen* screen = resolveTargetScreen(screenId);
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
    Layout* currentLayout = resolveScreenLayout(screenId);
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
    if (auto* selectorWindow = m_screenStates.value(screenId).zoneSelectorWindow) {
        selectorWindow->hide();
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

    // Zone appearance defaults (used when zone.useCustomColors is false) - match main overlay
    writeColorSettings(m_snapAssistWindow, m_settings);
    if (m_settings) {
        writeQmlProperty(m_snapAssistWindow, QStringLiteral("borderWidth"), m_settings->borderWidth());
        writeQmlProperty(m_snapAssistWindow, QStringLiteral("borderRadius"), m_settings->borderRadius());
    }

    // On reuse, restore exclusive keyboard grab via the mutable transport handle —
    // it was released in onSnapAssistWindowSelected() to keep the desktop responsive
    // during the D-Bus roundtrip. Fresh-create path already attaches with Exclusive
    // via PzRoles::SnapAssist, so nothing extra to do there.
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
    if (m_zoneSelectorVisible && !screenId.isEmpty()) {
        if (auto* selectorWindow = m_screenStates.value(screenId).zoneSelectorWindow) {
            selectorWindow->show();
        }
    }
}

bool OverlayService::isSnapAssistVisible() const
{
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
    std::optional<PhosphorLayer::Anchors> anchorsOverride;
    std::optional<QMargins> marginsOverride;
    const QRect physGeom = screen->geometry();
    const bool isVirtualScreen = screenGeom.isValid() && screenGeom != physGeom;
    if (isVirtualScreen) {
        anchorsOverride = PhosphorLayer::Anchors{PhosphorLayer::Anchor::Top, PhosphorLayer::Anchor::Left};
        const QRect clamped = screenGeom.intersected(physGeom);
        marginsOverride = QMargins(clamped.x() - physGeom.x(), clamped.y() - physGeom.y(), 0, 0);
    } else {
        anchorsOverride = PhosphorLayer::AnchorAll;
    }

    const QString scopeId = resolvedId.isEmpty() ? Utils::screenIdentifier(screen) : resolvedId;
    const auto role = PzRoles::SnapAssist.withScopePrefix(
        QStringLiteral("plasmazones-snap-assist-%1-%2").arg(scopeId).arg(++m_scopeGeneration));

    auto* surface = createLayerSurface(QUrl(QStringLiteral("qrc:/ui/SnapAssistOverlay.qml")), screen, role,
                                       "snap assist", QVariantMap(), anchorsOverride, marginsOverride);
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
    // Release exclusive keyboard grab immediately so the desktop remains
    // responsive while the D-Bus roundtrip to KWin processes the snap.
    // The window stays visible for potential reuse by showSnapAssist() continuation.
    if (m_snapAssistSurface) {
        if (auto* handle = m_snapAssistSurface->transport()) {
            handle->setKeyboardInteractivity(PhosphorLayer::KeyboardInteractivity::None);
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

    // Always destroy and recreate for fresh state. Pass the resolved geometry
    // so the Surface attaches with correct virtual-screen anchors + margins
    // (wlr-layer-shell doesn't let us reconfigure those post-attach).
    destroyLayoutPickerWindow();
    createLayoutPickerWindowFor(screen, screenGeom, resolvedId);
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

    // Anchors + margins were baked into the Surface by createLayoutPickerWindowFor above
    // using screenGeom, so positioning is already correct.
    assertWindowOnScreen(m_layoutPickerWindow, screen, screenGeom);
    m_layoutPickerWindow->setWidth(screenGeom.width());
    m_layoutPickerWindow->setHeight(screenGeom.height());
    m_layoutPickerSurface->show();
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
    std::optional<PhosphorLayer::Anchors> anchorsOverride;
    std::optional<QMargins> marginsOverride;
    const QRect physGeom = screen->geometry();
    const bool isVirtualScreen = screenGeom.isValid() && screenGeom != physGeom;
    if (isVirtualScreen) {
        anchorsOverride = PhosphorLayer::Anchors{PhosphorLayer::Anchor::Top, PhosphorLayer::Anchor::Left};
        const QRect clamped = screenGeom.intersected(physGeom);
        marginsOverride = QMargins(clamped.x() - physGeom.x(), clamped.y() - physGeom.y(), 0, 0);
    } else {
        anchorsOverride = PhosphorLayer::AnchorAll;
    }

    // Per-instance scope disambiguator so the compositor sees each open/close
    // cycle as a fresh surface (prevents configure-event rate-limiting on rapid
    // reopens).
    const QString scopeId = resolvedId.isEmpty() ? Utils::screenIdentifier(screen) : resolvedId;
    const auto role = PzRoles::LayoutPicker.withScopePrefix(
        QStringLiteral("plasmazones-layout-picker-%1-%2").arg(scopeId).arg(++m_scopeGeneration));

    auto* surface = createLayerSurface(QUrl(QStringLiteral("qrc:/ui/LayoutPickerOverlay.qml")), screen, role,
                                       "layout picker", QVariantMap(), anchorsOverride, marginsOverride);
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

    // Connect layoutSelected and dismissed signals from QML
    connect(window, SIGNAL(layoutSelected(QString)), this, SLOT(onLayoutPickerSelected(QString)));
    connect(window, SIGNAL(dismissed()), this, SLOT(hideLayoutPicker()));

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
            disconnect(m_layoutPickerWindow, &QWindow::visibleChanged, this, nullptr);
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
