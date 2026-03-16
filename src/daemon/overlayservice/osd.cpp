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
#include <LayerShellQt/Window>

namespace PlasmaZones {

namespace {

// Result of OSD window preparation
struct OsdWindowSetup
{
    QQuickWindow* window = nullptr;
    QRect screenGeom;
    qreal aspectRatio = 16.0 / 9.0;

    explicit operator bool() const
    {
        return window != nullptr;
    }
};

// Center an OSD/layer window on screen using LayerShellQt margins
void centerLayerWindowOnScreen(QQuickWindow* window, const QRect& screenGeom, int osdWidth, int osdHeight)
{
    if (!window) {
        return;
    }
    if (auto* layerWindow = LayerShellQt::Window::get(window)) {
        const int hMargin = qMax(0, (screenGeom.width() - osdWidth) / 2);
        const int vMargin = qMax(0, (screenGeom.height() - osdHeight) / 2);
        layerWindow->setAnchors(
            LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorBottom
                                          | LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight));
        layerWindow->setMargins(QMargins(hMargin, vMargin, hMargin, vMargin));
    }
}

// Calculate OSD size and center window
void sizeAndCenterOsd(QQuickWindow* window, const QRect& screenGeom, qreal aspectRatio)
{
    constexpr int osdWidth = 280;
    const int osdHeight = static_cast<int>(200 / aspectRatio) + 80;
    window->setWidth(osdWidth);
    window->setHeight(osdHeight);
    centerLayerWindowOnScreen(window, screenGeom, osdWidth, osdHeight);
}

} // namespace

bool OverlayService::prepareLayoutOsdWindow(QQuickWindow*& window, QRect& screenGeom, qreal& aspectRatio,
                                            const QString& screenName)
{
    // Resolve target screen: explicit name/ID > primary
    // Note: QCursor::pos() is NOT used here — it returns stale data for background
    // daemons on Wayland. Callers should always pass screenName from KWin effect data.
    // Accepts both connector name (e.g. "DP-2") and EDID-based screen ID (e.g. from currentScreenName).
    QScreen* screen = nullptr;
    if (!screenName.isEmpty()) {
        screen = Utils::findScreenByIdOrName(screenName);
    }
    if (!screen) {
        screen = Utils::primaryScreen();
    }
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

void OverlayService::showLayoutOsd(Layout* layout, const QString& screenName)
{
    if (!layout) {
        qCDebug(lcOverlay) << "No layout provided for OSD";
        return;
    }

    if (layout->zones().isEmpty()) {
        qCDebug(lcOverlay) << "Skipping OSD for empty layout=" << layout->name();
        return;
    }

    QQuickWindow* window = nullptr;
    QRect screenGeom;
    qreal aspectRatio = 0;
    if (!prepareLayoutOsdWindow(window, screenGeom, aspectRatio, screenName)) {
        return;
    }

    writeQmlProperty(window, QStringLiteral("locked"), false);
    writeQmlProperty(window, QStringLiteral("layoutId"), layout->id().toString());
    writeQmlProperty(window, QStringLiteral("layoutName"), layout->name());
    writeQmlProperty(window, QStringLiteral("screenAspectRatio"), aspectRatio);
    writeQmlProperty(window, QStringLiteral("category"), static_cast<int>(LayoutCategory::Manual));
    writeQmlProperty(window, QStringLiteral("autoAssign"), layout->autoAssign());
    writeQmlProperty(window, QStringLiteral("zones"), LayoutUtils::zonesToVariantList(layout, ZoneField::Full));
    writeFontProperties(window, m_settings);

    sizeAndCenterOsd(window, screenGeom, aspectRatio);
    QMetaObject::invokeMethod(window, "show");
    qCInfo(lcOverlay) << "Layout OSD: layout=" << layout->name() << "screen=" << screenName;
}

void OverlayService::showLockedLayoutOsd(Layout* layout, const QString& screenName)
{
    if (!layout) {
        return;
    }

    QQuickWindow* window = nullptr;
    QRect screenGeom;
    qreal aspectRatio = 0;
    if (!prepareLayoutOsdWindow(window, screenGeom, aspectRatio, screenName)) {
        return;
    }

    writeQmlProperty(window, QStringLiteral("locked"), true);
    writeQmlProperty(window, QStringLiteral("layoutId"), layout->id().toString());
    writeQmlProperty(window, QStringLiteral("layoutName"), layout->name());
    writeQmlProperty(window, QStringLiteral("screenAspectRatio"), aspectRatio);
    writeQmlProperty(window, QStringLiteral("category"), static_cast<int>(LayoutCategory::Manual));
    writeQmlProperty(window, QStringLiteral("autoAssign"), layout->autoAssign());
    writeQmlProperty(window, QStringLiteral("zones"),
                     layout->zones().isEmpty() ? QVariantList()
                                               : LayoutUtils::zonesToVariantList(layout, ZoneField::Full));
    writeFontProperties(window, m_settings);

    sizeAndCenterOsd(window, screenGeom, aspectRatio);
    QMetaObject::invokeMethod(window, "show");
    qCInfo(lcOverlay) << "Locked OSD: layout=" << layout->name() << "screen=" << screenName;
}

void OverlayService::showLayoutOsd(const QString& id, const QString& name, const QVariantList& zones, int category,
                                   bool autoAssign, const QString& screenName)
{
    if (zones.isEmpty()) {
        qCDebug(lcOverlay) << "Skipping OSD for empty layout=" << name;
        return;
    }

    QQuickWindow* window = nullptr;
    QRect screenGeom;
    qreal aspectRatio = 0;
    if (!prepareLayoutOsdWindow(window, screenGeom, aspectRatio, screenName)) {
        return;
    }

    writeQmlProperty(window, QStringLiteral("layoutId"), id);
    writeQmlProperty(window, QStringLiteral("layoutName"), name);
    writeQmlProperty(window, QStringLiteral("screenAspectRatio"), aspectRatio);
    writeQmlProperty(window, QStringLiteral("category"), category);
    writeQmlProperty(window, QStringLiteral("autoAssign"), autoAssign);
    writeQmlProperty(window, QStringLiteral("zones"), zones);
    writeFontProperties(window, m_settings);

    sizeAndCenterOsd(window, screenGeom, aspectRatio);
    QMetaObject::invokeMethod(window, "show");
    qCInfo(lcOverlay) << "Layout OSD: name=" << name << "category=" << category << "screen=" << screenName;
}

void OverlayService::hideLayoutOsd()
{
    for (auto* window : std::as_const(m_layoutOsdWindows)) {
        if (window && window->isVisible()) {
            QMetaObject::invokeMethod(window, "hide");
        }
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
        });
        m_screenAddedConnected = true;
    }
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

    // Configure LayerShellQt for Wayland overlay (prevents window from appearing in taskbar)
    if (auto* layerWindow = LayerShellQt::Window::get(window)) {
        layerWindow->setScreen(screen);
        layerWindow->setLayer(LayerShellQt::Window::LayerOverlay);
        layerWindow->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
        // Anchors will be set dynamically in showLayoutOsd() based on window size
        layerWindow->setScope(QStringLiteral("plasmazones-layout-osd-%1").arg(screen->name()));
        layerWindow->setExclusiveZone(-1);
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
        window->close();
        window->deleteLater();
    }
}

void OverlayService::showNavigationOsd(bool success, const QString& action, const QString& reason,
                                       const QString& sourceZoneId, const QString& targetZoneId,
                                       const QString& screenName)
{
    qCDebug(lcOverlay) << "showNavigationOsd called: action=" << action << "reason=" << reason
                       << "screen=" << screenName << "success=" << success;

    // Only show OSD for successful actions - failures (no windows, no zones, etc.) don't need feedback
    if (!success) {
        qCDebug(lcOverlay) << "Skipping navigation OSD for failure:" << action << reason;
        return;
    }

    // Deduplicate: Skip if same action+reason within 200ms (prevents duplicate from Qt signal + D-Bus signal)
    QString actionKey = action + QLatin1String(":") + reason;
    if (actionKey == m_lastNavigationAction + QLatin1String(":") + m_lastNavigationReason
        && m_lastNavigationTime.isValid() && m_lastNavigationTime.elapsed() < 200) {
        qCDebug(lcOverlay) << "Skipping duplicate navigation OSD:" << action << reason;
        return;
    }
    m_lastNavigationAction = action;
    m_lastNavigationReason = reason;
    m_lastNavigationTime.restart();

    // Show on the screen where the navigation occurred, fallback to primary
    // Accepts both connector name and EDID-based screen ID for flexibility
    QScreen* screen = Utils::findScreenByIdOrName(screenName);
    if (!screen) {
        screen = Utils::primaryScreen();
    }
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

    // Create window if needed
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

    // Hide any existing navigation OSD before showing new one (prevent overlap)
    hideNavigationOsd();

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
    for (auto* window : std::as_const(m_navigationOsdWindows)) {
        if (window && window->isVisible()) {
            QMetaObject::invokeMethod(window, "hide");
        }
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

    // Configure LayerShellQt for Wayland overlay
    if (auto* layerWindow = LayerShellQt::Window::get(window)) {
        layerWindow->setScreen(screen);
        layerWindow->setLayer(LayerShellQt::Window::LayerOverlay);
        layerWindow->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
        layerWindow->setScope(QStringLiteral("plasmazones-navigation-osd-%1").arg(screen->name()));
        layerWindow->setExclusiveZone(-1);
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
        window->close();
        window->deleteLater();
    }
    // Clear failed flag when destroying window
    m_navigationOsdCreationFailed.remove(screen);
}

} // namespace PlasmaZones
