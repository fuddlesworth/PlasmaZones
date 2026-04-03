// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "internal.h"
#include "../overlayservice.h"
#include "../../core/logging.h"
#include "../../core/layout.h"
#include "../../core/layoututils.h"
#include "../../core/utils.h"
#include <QQuickWindow>
#include <QScreen>
#include <QQmlEngine>
#include <QGuiApplication>
#include "../../core/layersurface.h"

namespace PlasmaZones {

namespace {

// Center an OSD/layer window on screen using layer surface margins.
// Precondition: the window must already have a LayerSurface (created before show()).
// This function retrieves the existing LayerSurface — it does not create one.
void centerLayerWindowOnScreen(QQuickWindow* window, const QRect& screenGeom, int osdWidth, int osdHeight)
{
    if (!window) {
        return;
    }
    auto* layerSurface = LayerSurface::find(window);
    if (!layerSurface) {
        qCWarning(lcOverlay) << "centerLayerWindowOnScreen: no LayerSurface for window"
                             << "— was LayerSurface::get() called before show()?";
        return;
    }
    const int hMargin = qMax(0, (screenGeom.width() - osdWidth) / 2);
    const int vMargin = qMax(0, (screenGeom.height() - osdHeight) / 2);
    // Batch anchors + margins into a single propertiesChanged() emission
    // to avoid two applyProperties()+wl_surface_commit round-trips.
    LayerSurface::BatchGuard batch(layerSurface);
    layerSurface->setAnchors(LayerSurface::AnchorAll);
    layerSurface->setMargins(QMargins(hMargin, vMargin, hMargin, vMargin));
}

// Calculate OSD size and center window
void sizeAndCenterOsd(QQuickWindow* window, const QRect& screenGeom, qreal previewAspectRatio)
{
    constexpr int osdWidth = 280;
    // Clamp AR to sane range to prevent absurd OSD sizes
    const qreal safeAR = qBound(0.5, previewAspectRatio, 4.0);
    const int osdHeight = static_cast<int>(200 / safeAR) + 80;
    window->setWidth(osdWidth);
    window->setHeight(osdHeight);
    centerLayerWindowOnScreen(window, screenGeom, osdWidth, osdHeight);
}

} // namespace

bool OverlayService::prepareLayoutOsdWindow(QQuickWindow*& window, QRect& screenGeom, qreal& aspectRatio,
                                            const QString& screenId)
{
    // Resolve target screen: explicit name/ID > primary
    // Note: QCursor::pos() is NOT used here — it returns stale data for background
    // daemons on Wayland. Callers should always pass screenId from KWin effect data.
    // Accepts both connector name (e.g. "DP-2") and EDID-based screen ID (e.g. from currentScreenName).
    QScreen* screen = resolveTargetScreen(screenId);
    if (!screen) {
        qCWarning(lcOverlay) << "No screen available for layout OSD";
        return false;
    }

    if (!m_layoutOsdWindows.contains(screen)) {
        createLayoutOsdWindow(screen);
    }

    window = m_layoutOsdWindows.value(screen);
    if (!window) {
        qCWarning(lcOverlay) << "Failed to get layout OSD window";
        return false;
    }

    assertWindowOnScreen(window, screen);

    screenGeom = screen->geometry();
    aspectRatio =
        (screenGeom.height() > 0) ? static_cast<qreal>(screenGeom.width()) / screenGeom.height() : (16.0 / 9.0);
    aspectRatio = qBound(0.5, aspectRatio, 4.0);

    return true;
}

void OverlayService::showLayoutOsd(Layout* layout, const QString& screenId)
{
    if (!layout) {
        qCDebug(lcOverlay) << "No layout provided for OSD";
        return;
    }

    if (layout->zones().isEmpty()) {
        qCDebug(lcOverlay) << "Skipping OSD for empty layout=" << layout->name();
        return;
    }

    showLayoutOsdImpl(layout, screenId, false);
}

void OverlayService::showLockedLayoutOsd(Layout* layout, const QString& screenId)
{
    if (!layout) {
        return;
    }

    showLayoutOsdImpl(layout, screenId, true);
}

void OverlayService::showLayoutOsdImpl(Layout* layout, const QString& screenId, bool locked)
{
    QQuickWindow* window = nullptr;
    QRect screenGeom;
    qreal aspectRatio = 0;
    if (!prepareLayoutOsdWindow(window, screenGeom, aspectRatio, screenId)) {
        return;
    }

    writeQmlProperty(window, QStringLiteral("locked"), locked);
    writeQmlProperty(window, QStringLiteral("layoutId"), layout->id().toString());
    writeQmlProperty(window, QStringLiteral("layoutName"), layout->name());
    writeQmlProperty(window, QStringLiteral("screenAspectRatio"), aspectRatio);
    writeQmlProperty(window, QStringLiteral("aspectRatioClass"),
                     ScreenClassification::toString(layout->aspectRatioClass()));
    writeQmlProperty(window, QStringLiteral("category"), static_cast<int>(LayoutCategory::Manual));
    writeQmlProperty(window, QStringLiteral("autoAssign"), layout->autoAssign());
    writeAutotileMetadata(window, false, false);
    writeQmlProperty(window, QStringLiteral("zones"),
                     layout->zones().isEmpty() ? QVariantList()
                                               : LayoutUtils::zonesToVariantList(layout, ZoneField::Full));
    writeFontProperties(window, m_settings);

    qreal layoutAR = ScreenClassification::aspectRatioForClass(layout->aspectRatioClass(), aspectRatio);
    sizeAndCenterOsd(window, screenGeom, layoutAR);
    QMetaObject::invokeMethod(window, "show");
    qCInfo(lcOverlay) << (locked ? "Locked" : "Layout") << "OSD: layout=" << layout->name() << "screen=" << screenId;
}

void OverlayService::showLayoutOsd(const QString& id, const QString& name, const QVariantList& zones, int category,
                                   bool autoAssign, const QString& screenId, bool showMasterDot,
                                   bool producesOverlappingZones, const QString& zoneNumberDisplay, int masterCount)
{
    if (zones.isEmpty()) {
        qCDebug(lcOverlay) << "Skipping OSD for empty layout=" << name;
        return;
    }

    QQuickWindow* window = nullptr;
    QRect screenGeom;
    qreal aspectRatio = 0;
    if (!prepareLayoutOsdWindow(window, screenGeom, aspectRatio, screenId)) {
        return;
    }

    // Reset locked state — window is reused across show calls, so a prior
    // showLockedLayoutOsd() would leave the lock overlay stuck on.
    writeQmlProperty(window, QStringLiteral("locked"), false);
    writeQmlProperty(window, QStringLiteral("layoutId"), id);
    writeQmlProperty(window, QStringLiteral("layoutName"), name);
    writeQmlProperty(window, QStringLiteral("screenAspectRatio"), aspectRatio);
    // Resolve aspectRatioClass from Layout* if available
    qreal layoutAR = aspectRatio;
    {
        QString arClass = QStringLiteral("any");
        auto uuidOpt = Utils::parseUuid(id);
        if (uuidOpt && m_layoutManager) {
            Layout* layout = m_layoutManager->layoutById(*uuidOpt);
            if (layout) {
                arClass = ScreenClassification::toString(layout->aspectRatioClass());
                layoutAR = ScreenClassification::aspectRatioForClass(layout->aspectRatioClass(), aspectRatio);
            }
        }
        writeQmlProperty(window, QStringLiteral("aspectRatioClass"), arClass);
    }
    writeQmlProperty(window, QStringLiteral("category"), category);
    writeQmlProperty(window, QStringLiteral("autoAssign"), autoAssign);
    writeAutotileMetadata(window, showMasterDot, producesOverlappingZones, zoneNumberDisplay, masterCount);
    writeQmlProperty(window, QStringLiteral("zones"), zones);
    writeFontProperties(window, m_settings);

    sizeAndCenterOsd(window, screenGeom, layoutAR);
    QMetaObject::invokeMethod(window, "show");
    qCInfo(lcOverlay) << "Layout OSD: name=" << name << "category=" << category << "screen=" << screenId;
}

void OverlayService::hideLayoutOsd()
{
    // Per-screen destroy: only destroy the sending window's screen so multi-screen
    // desktop-switch OSDs don't kill each other mid-animation.
    auto* senderWindow = qobject_cast<QQuickWindow*>(sender());
    if (senderWindow) {
        for (auto it = m_layoutOsdWindows.constBegin(); it != m_layoutOsdWindows.constEnd(); ++it) {
            if (it.value() == senderWindow) {
                destroyLayoutOsdWindow(it.key());
                return;
            }
        }
    }
    // Fallback: no sender (direct call) — destroy all
    const QList<QScreen*> screens = m_layoutOsdWindows.keys();
    for (auto* screen : screens) {
        destroyLayoutOsdWindow(screen);
    }
}

void OverlayService::warmUpLayoutOsd()
{
    const auto screens = QGuiApplication::screens();
    for (QScreen* screen : screens) {
        if (!m_layoutOsdWindows.contains(screen)) {
            createLayoutOsdWindow(screen);
        }
    }
    qCInfo(lcOverlay) << "Pre-warmed Layout OSD windows for" << screens.size() << "screens";

    // Also warm up screens added later (hot-plug) so the first OSD on a
    // newly connected screen doesn't incur the ~100-300ms QML compilation delay.
    // Note: Qt::UniqueConnection cannot be used with lambdas (causes ASSERT crash in Qt6).
    // Use a bool guard instead to prevent duplicate connections.
    if (!m_screenAddedConnected) {
        connect(qGuiApp, &QGuiApplication::screenAdded, this, [this](QScreen* screen) {
            if (!m_layoutOsdWindows.contains(screen)) {
                createLayoutOsdWindow(screen);
            }
            if (!m_navigationOsdWindows.contains(screen)) {
                createNavigationOsdWindow(screen);
            }
        });
        m_screenAddedConnected = true;
    }
}

void OverlayService::warmUpNavigationOsd()
{
    const auto screens = QGuiApplication::screens();
    for (QScreen* screen : screens) {
        if (!m_navigationOsdWindows.contains(screen)) {
            createNavigationOsdWindow(screen);
        }
    }
    qCInfo(lcOverlay) << "Pre-warmed Navigation OSD windows for" << screens.size() << "screens";
}

void OverlayService::createLayoutOsdWindow(QScreen* screen)
{
    if (m_layoutOsdWindows.contains(screen)) {
        return;
    }

    auto* window = createQmlWindow(QUrl(QStringLiteral("qrc:/ui/LayoutOsd.qml")), screen, "layout OSD");
    if (!window) {
        return;
    }

    // Configure layer surface for Wayland overlay (prevents window from appearing in taskbar)
    // Anchors will be set dynamically in showLayoutOsd() based on window size
    if (!configureLayerSurface(window, screen, LayerSurface::LayerOverlay, LayerSurface::KeyboardInteractivityNone,
                               QStringLiteral("plasmazones-layout-osd-%1").arg(Utils::screenIdentifier(screen)))) {
        qCWarning(lcOverlay) << "Failed to configure layer surface for layout OSD on" << screen->name();
        delete window;
        return;
    }

    auto layoutOsdConn = connect(window, SIGNAL(dismissed()), this, SLOT(hideLayoutOsd()));
    if (!layoutOsdConn) {
        qCWarning(lcOverlay) << "Failed to connect dismissed signal for layout OSD on screen" << screen->name();
    }
    window->setVisible(false);
    m_layoutOsdWindows.insert(screen, window);
}

void OverlayService::destroyLayoutOsdWindow(QScreen* screen)
{
    if (auto* window = m_layoutOsdWindows.take(screen)) {
        // Disconnect so no signals (e.g. geometryChanged) are delivered to a window we're destroying
        disconnect(screen, nullptr, window, nullptr);
        window->close();
        window->destroy();
        window->deleteLater();
    }
}

void OverlayService::showNavigationOsd(bool success, const QString& action, const QString& reason,
                                       const QString& sourceZoneId, const QString& targetZoneId,
                                       const QString& screenId)
{
    qCDebug(lcOverlay) << "showNavigationOsd called: action=" << action << "reason=" << reason << "screen=" << screenId
                       << "success=" << success;

    // Only show OSD for successful actions - failures (no windows, no zones, etc.) don't need feedback
    if (!success) {
        qCDebug(lcOverlay) << "Skipping navigation OSD for failure:" << action << reason;
        return;
    }

    // Deduplicate: Skip if same action+reason within 200ms (prevents duplicate from Qt signal + D-Bus signal)
    const QString actionKey = action + QLatin1Char(':') + reason;
    if (actionKey == m_lastNavigationActionKey && m_lastNavigationTime.isValid()
        && m_lastNavigationTime.elapsed() < 200) {
        qCDebug(lcOverlay) << "Skipping duplicate navigation OSD:" << action << reason;
        return;
    }
    m_lastNavigationActionKey = actionKey;
    m_lastNavigationTime.restart();

    // Show on the screen where the navigation occurred, fallback to primary
    // Accepts both connector name and EDID-based screen ID for flexibility
    QScreen* screen = resolveTargetScreen(screenId);
    if (!screen) {
        qCWarning(lcOverlay) << "No screen available for navigation OSD";
        return;
    }

    // Resolve per-screen layout (not the global m_layout which may belong to another screen)
    // Float, algorithm, rotate, and autotile-only actions don't need layout/zones
    static const QStringList noLayoutActions{QStringLiteral("float"),        QStringLiteral("algorithm"),
                                             QStringLiteral("rotate"),       QStringLiteral("focus_master"),
                                             QStringLiteral("swap_master"),  QStringLiteral("master_ratio"),
                                             QStringLiteral("master_count"), QStringLiteral("retile")};
    const bool needsLayout = !noLayoutActions.contains(action);
    Layout* screenLayout = resolveScreenLayout(screen);
    if ((needsLayout && !screenLayout) || (screenLayout && screenLayout->zones().isEmpty() && needsLayout)) {
        qCDebug(lcOverlay) << "No layout or zones for navigation OSD: screen=" << screen->name()
                           << "layout=" << (screenLayout ? screenLayout->name() : QStringLiteral("null"))
                           << "zones=" << (screenLayout ? screenLayout->zones().size() : 0) << "action=" << action;
        return;
    }

    // Reuse existing window for this screen (create only if not in map).
    // The window stays alive and visible across rapid navigation calls —
    // QML show() resets the animation and restarts the dismiss timer each time.
    // Cleanup happens when the dismiss timer expires: dismissed() signal →
    // hideNavigationOsd() slot → destroyNavigationOsdWindow(). This matches
    // the layout OSD pattern and avoids Vulkan surface create/destroy churn
    // that causes resource exhaustion and daemon freezes during rapid input.
    if (!m_navigationOsdWindows.contains(screen)) {
        // Only try to create if we haven't failed before (prevents log spam)
        if (!m_navigationOsdCreationFailed.value(screen, false)) {
            createNavigationOsdWindow(screen);
        }
    }

    auto* window = m_navigationOsdWindows.value(screen);
    if (!window) {
        // Only warn once per screen to prevent log spam
        if (!m_navigationOsdCreationFailed.value(screen, false)) {
            qCWarning(lcOverlay) << "Failed to get navigation OSD window for screen=" << screen->name();
            m_navigationOsdCreationFailed.insert(screen, true);
        }
        qCDebug(lcOverlay) << "No navigation OSD window for screen=" << screen->name();
        return;
    }

    // Process reason field - for rotation/resnap, extract window count
    // Format: "clockwise:N" or "counterclockwise:N" or "resnap:N" where N is window count
    int windowCount = 1;
    QString displayReason = reason;
    if (reason.contains(QLatin1Char(':'))) {
        QStringList parts = reason.split(QLatin1Char(':'));
        if (parts.size() >= 2) {
            bool ok = false;
            int count = parts.at(1).toInt(&ok);
            if (ok && count > 0) {
                windowCount = count;
            }
            if (action == QStringLiteral("rotate")) {
                displayReason = parts.at(0); // "clockwise" or "counterclockwise"
            }
            // resnap keeps full reason for displayReason (optional)
        }
    }

    // Set OSD data
    writeQmlProperty(window, QStringLiteral("success"), success);
    writeQmlProperty(window, QStringLiteral("action"), action);
    writeQmlProperty(window, QStringLiteral("reason"), displayReason);
    writeQmlProperty(window, QStringLiteral("windowCount"), windowCount);

    // Pass source zone ID for swap operations
    writeQmlProperty(window, QStringLiteral("sourceZoneId"), sourceZoneId);

    // Build highlighted zone IDs list (target zones)
    QStringList highlightedZoneIds;
    if (!targetZoneId.isEmpty()) {
        highlightedZoneIds.append(targetZoneId);
    }
    writeQmlProperty(window, QStringLiteral("highlightedZoneIds"), highlightedZoneIds);

    // Use shared LayoutUtils with minimal fields for zone number lookup
    // (only need zoneId and zoneNumber, not name/appearance)
    QVariantList zonesList = LayoutUtils::zonesToVariantList(screenLayout, ZoneField::Minimal);
    writeQmlProperty(window, QStringLiteral("zones"), zonesList);

    // Ensure the window is on the correct Wayland output (must come before sizing —
    // assertWindowOnScreen calls setGeometry(screen) which would override setWidth/setHeight)
    assertWindowOnScreen(window, screen);

    // Size and center: setWidth/setHeight AFTER assertWindowOnScreen so the final
    // QWindow geometry matches the OSD size (same pattern as sizeAndCenterOsd for LayoutOsd)
    const QRect screenGeom = screen->geometry();
    const int osdWidth = 240; // Compact width for text
    const int osdHeight = 70; // Text message + margins
    window->setWidth(osdWidth);
    window->setHeight(osdHeight);
    centerLayerWindowOnScreen(window, screenGeom, osdWidth, osdHeight);

    // Show with animation
    QMetaObject::invokeMethod(window, "show");

    qCInfo(lcOverlay) << "Showing navigation OSD: success=" << success << "action=" << action << "reason=" << reason
                      << "highlightedZones=" << highlightedZoneIds;
}

void OverlayService::hideNavigationOsd()
{
    // Per-screen destroy (same rationale as hideLayoutOsd).
    auto* senderWindow = qobject_cast<QQuickWindow*>(sender());
    if (senderWindow) {
        for (auto it = m_navigationOsdWindows.constBegin(); it != m_navigationOsdWindows.constEnd(); ++it) {
            if (it.value() == senderWindow) {
                destroyNavigationOsdWindow(it.key());
                return;
            }
        }
    }
    // Fallback: no sender (direct call) — destroy all
    const QList<QScreen*> screens = m_navigationOsdWindows.keys();
    for (auto* screen : screens) {
        destroyNavigationOsdWindow(screen);
    }
}

void OverlayService::createNavigationOsdWindow(QScreen* screen)
{
    if (m_navigationOsdWindows.contains(screen)) {
        return;
    }

    auto* window = createQmlWindow(QUrl(QStringLiteral("qrc:/ui/NavigationOsd.qml")), screen, "navigation OSD");
    if (!window) {
        m_navigationOsdCreationFailed.insert(screen, true);
        return;
    }

    // Configure layer surface for Wayland overlay
    if (!configureLayerSurface(window, screen, LayerSurface::LayerOverlay, LayerSurface::KeyboardInteractivityNone,
                               QStringLiteral("plasmazones-navigation-osd-%1").arg(Utils::screenIdentifier(screen)))) {
        qCWarning(lcOverlay) << "Failed to configure layer surface for navigation OSD on" << screen->name();
        m_navigationOsdCreationFailed.insert(screen, true);
        delete window;
        return;
    }

    auto navOsdConn = connect(window, SIGNAL(dismissed()), this, SLOT(hideNavigationOsd()));
    if (!navOsdConn) {
        qCWarning(lcOverlay) << "Failed to connect dismissed signal for navigation OSD on screen" << screen->name();
    }
    window->setVisible(false);
    m_navigationOsdWindows.insert(screen, window);
    m_navigationOsdCreationFailed.remove(screen);
}

void OverlayService::destroyNavigationOsdWindow(QScreen* screen)
{
    if (auto* window = m_navigationOsdWindows.take(screen)) {
        // Disconnect so no signals (e.g. geometryChanged) are delivered to a window we're destroying
        disconnect(screen, nullptr, window, nullptr);
        window->close();
        window->destroy();
        window->deleteLater();
    }
    // Clear failed flag when destroying window
    m_navigationOsdCreationFailed.remove(screen);
}

} // namespace PlasmaZones
