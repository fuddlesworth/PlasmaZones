// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "internal.h"
#include "../overlayservice.h"
#include "../../core/logging.h"
#include "../../core/layout.h"
#include "../../core/layoututils.h"
#include "../../core/screenmanager.h"
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

// Center an OSD/layer window within a screen geometry using LayerShellQt margins.
// physScreenGeom is the full physical screen; targetGeom is the area to center within
// (same as physScreenGeom for physical screens, or a sub-region for virtual screens).
void centerLayerWindowOnScreen(QQuickWindow* window, const QRect& physScreenGeom, const QRect& targetGeom, int osdWidth,
                               int osdHeight)
{
    if (!window) {
        return;
    }
    if (auto* layerWindow = LayerShellQt::Window::get(window)) {
        // Compute center position within the target area, expressed as margins from physical screen edges
        const int targetCenterX = (targetGeom.x() - physScreenGeom.x()) + qMax(0, (targetGeom.width() - osdWidth) / 2);
        const int targetCenterY =
            (targetGeom.y() - physScreenGeom.y()) + qMax(0, (targetGeom.height() - osdHeight) / 2);
        const int rightMargin = qMax(0, physScreenGeom.width() - targetCenterX - osdWidth);
        const int bottomMargin = qMax(0, physScreenGeom.height() - targetCenterY - osdHeight);

        layerWindow->setAnchors(
            LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorBottom
                                          | LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight));
        layerWindow->setMargins(QMargins(targetCenterX, targetCenterY, rightMargin, bottomMargin));
    }
}

// Calculate OSD size and center window
void sizeAndCenterOsd(QQuickWindow* window, QScreen* physScreen, const QRect& targetGeom, qreal previewAspectRatio)
{
    constexpr int osdWidth = 280;
    // Clamp AR to sane range to prevent absurd OSD sizes
    const qreal safeAR = qBound(0.5, previewAspectRatio, 4.0);
    const int osdHeight = static_cast<int>(200 / safeAR) + 80;
    window->setWidth(osdWidth);
    window->setHeight(osdHeight);
    const QRect physGeom = physScreen ? physScreen->geometry() : targetGeom;
    centerLayerWindowOnScreen(window, physGeom, targetGeom, osdWidth, osdHeight);
}

} // namespace

bool OverlayService::prepareLayoutOsdWindow(QQuickWindow*& window, QScreen*& outPhysScreen, QRect& screenGeom,
                                            qreal& aspectRatio, const QString& screenId)
{
    // Resolve target screen via ScreenManager for virtual screen support
    auto* mgr = ScreenManager::instance();
    QScreen* physScreen = mgr ? mgr->physicalQScreenFor(screenId) : nullptr;
    if (!physScreen && !screenId.isEmpty()) {
        physScreen = Utils::findScreenByIdOrName(screenId);
    }
    if (!physScreen) {
        physScreen = Utils::primaryScreen();
    }
    if (!physScreen) {
        qCWarning(lcOverlay) << "No screen available for layout OSD";
        return false;
    }

    outPhysScreen = physScreen;

    // Use virtual screen geometry if applicable, otherwise physical
    QRect vsGeom = (mgr && !screenId.isEmpty()) ? mgr->screenGeometry(screenId) : QRect();
    screenGeom = vsGeom.isValid() ? vsGeom : physScreen->geometry();

    QString effectiveId = screenId.isEmpty() ? Utils::screenIdentifier(physScreen) : screenId;

    if (!m_layoutOsdWindows.contains(effectiveId)) {
        createLayoutOsdWindow(effectiveId, physScreen, screenGeom);
    }

    window = m_layoutOsdWindows.value(effectiveId);
    if (!window) {
        qCWarning(lcOverlay) << "Failed to get layout OSD window";
        return false;
    }

    assertWindowOnScreen(window, physScreen, screenGeom);

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

    QQuickWindow* window = nullptr;
    QScreen* physScreen = nullptr;
    QRect screenGeom;
    qreal aspectRatio = 0;
    if (!prepareLayoutOsdWindow(window, physScreen, screenGeom, aspectRatio, screenId)) {
        return;
    }

    writeQmlProperty(window, QStringLiteral("locked"), false);
    writeQmlProperty(window, QStringLiteral("layoutId"), layout->id().toString());
    writeQmlProperty(window, QStringLiteral("layoutName"), layout->name());
    writeQmlProperty(window, QStringLiteral("screenAspectRatio"), aspectRatio);
    writeQmlProperty(window, QStringLiteral("aspectRatioClass"),
                     ScreenClassification::toString(layout->aspectRatioClass()));
    writeQmlProperty(window, QStringLiteral("category"), static_cast<int>(LayoutCategory::Manual));
    writeQmlProperty(window, QStringLiteral("autoAssign"), layout->autoAssign());
    writeQmlProperty(window, QStringLiteral("zones"), LayoutUtils::zonesToVariantList(layout, ZoneField::Full));
    writeFontProperties(window, m_settings);

    // Size OSD using layout's intended AR for correct preview proportions
    qreal layoutAR = ScreenClassification::aspectRatioForClass(layout->aspectRatioClass(), aspectRatio);
    sizeAndCenterOsd(window, physScreen, screenGeom, layoutAR);
    QMetaObject::invokeMethod(window, "show");
    qCInfo(lcOverlay) << "Layout OSD: layout=" << layout->name() << "screen=" << screenId;
}

void OverlayService::showLockedLayoutOsd(Layout* layout, const QString& screenId)
{
    if (!layout) {
        return;
    }

    QQuickWindow* window = nullptr;
    QScreen* physScreen = nullptr;
    QRect screenGeom;
    qreal aspectRatio = 0;
    if (!prepareLayoutOsdWindow(window, physScreen, screenGeom, aspectRatio, screenId)) {
        return;
    }

    writeQmlProperty(window, QStringLiteral("locked"), true);
    writeQmlProperty(window, QStringLiteral("layoutId"), layout->id().toString());
    writeQmlProperty(window, QStringLiteral("layoutName"), layout->name());
    writeQmlProperty(window, QStringLiteral("screenAspectRatio"), aspectRatio);
    writeQmlProperty(window, QStringLiteral("aspectRatioClass"),
                     ScreenClassification::toString(layout->aspectRatioClass()));
    writeQmlProperty(window, QStringLiteral("category"), static_cast<int>(LayoutCategory::Manual));
    writeQmlProperty(window, QStringLiteral("autoAssign"), layout->autoAssign());
    writeQmlProperty(window, QStringLiteral("zones"),
                     layout->zones().isEmpty() ? QVariantList()
                                               : LayoutUtils::zonesToVariantList(layout, ZoneField::Full));
    writeFontProperties(window, m_settings);

    qreal layoutAR = ScreenClassification::aspectRatioForClass(layout->aspectRatioClass(), aspectRatio);
    sizeAndCenterOsd(window, physScreen, screenGeom, layoutAR);
    QMetaObject::invokeMethod(window, "show");
    qCInfo(lcOverlay) << "Locked OSD: layout=" << layout->name() << "screen=" << screenId;
}

void OverlayService::showLayoutOsd(const QString& id, const QString& name, const QVariantList& zones, int category,
                                   bool autoAssign, const QString& screenId)
{
    if (zones.isEmpty()) {
        qCDebug(lcOverlay) << "Skipping OSD for empty layout=" << name;
        return;
    }

    QQuickWindow* window = nullptr;
    QScreen* physScreen = nullptr;
    QRect screenGeom;
    qreal aspectRatio = 0;
    if (!prepareLayoutOsdWindow(window, physScreen, screenGeom, aspectRatio, screenId)) {
        return;
    }

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
    writeQmlProperty(window, QStringLiteral("zones"), zones);
    writeFontProperties(window, m_settings);

    sizeAndCenterOsd(window, physScreen, screenGeom, layoutAR);
    QMetaObject::invokeMethod(window, "show");
    qCInfo(lcOverlay) << "Layout OSD: name=" << name << "category=" << category << "screen=" << screenId;
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
    auto* mgr = ScreenManager::instance();
    const QStringList effectiveIds = mgr ? mgr->effectiveScreenIds() : QStringList();

    if (!effectiveIds.isEmpty()) {
        // Virtual-screen-aware warm-up: create OSD for each effective screen
        for (const QString& sid : effectiveIds) {
            if (!m_layoutOsdWindows.contains(sid)) {
                QScreen* physScreen = mgr->physicalQScreenFor(sid);
                QRect geom = mgr->screenGeometry(sid);
                if (physScreen && geom.isValid()) {
                    createLayoutOsdWindow(sid, physScreen, geom);
                }
            }
        }
        qCInfo(lcOverlay) << "Pre-warmed Layout OSD windows for" << effectiveIds.size() << "effective screens";
    } else {
        // Fallback: no ScreenManager or no effective IDs, warm up for physical screens
        const auto screens = QGuiApplication::screens();
        for (QScreen* screen : screens) {
            QString sid = Utils::screenIdentifier(screen);
            if (!m_layoutOsdWindows.contains(sid)) {
                createLayoutOsdWindow(sid, screen, screen->geometry());
            }
        }
        qCInfo(lcOverlay) << "Pre-warmed Layout OSD windows for" << QGuiApplication::screens().size() << "screens";
    }

    // Also warm up screens added later (hot-plug)
    if (!m_screenAddedConnected) {
        connect(qGuiApp, &QGuiApplication::screenAdded, this, [this](QScreen* screen) {
            auto* mgr2 = ScreenManager::instance();
            QString physId = Utils::screenIdentifier(screen);
            if (mgr2 && mgr2->hasVirtualScreens(physId)) {
                for (const QString& vsId : mgr2->virtualScreenIdsFor(physId)) {
                    if (!m_layoutOsdWindows.contains(vsId)) {
                        QRect vsGeom = mgr2->screenGeometry(vsId);
                        if (vsGeom.isValid()) {
                            createLayoutOsdWindow(vsId, screen, vsGeom);
                        }
                    }
                }
            } else {
                if (!m_layoutOsdWindows.contains(physId)) {
                    QRect geom = mgr2 ? mgr2->screenGeometry(physId) : screen->geometry();
                    if (!geom.isValid()) {
                        geom = screen->geometry();
                    }
                    createLayoutOsdWindow(physId, screen, geom);
                }
            }
        });
        m_screenAddedConnected = true;
    }
}

void OverlayService::createLayoutOsdWindow(const QString& screenId, QScreen* physScreen, const QRect& screenGeom)
{
    Q_UNUSED(screenGeom)
    if (m_layoutOsdWindows.contains(screenId)) {
        return;
    }

    auto* window = createQmlWindow(QUrl(QStringLiteral("qrc:/ui/LayoutOsd.qml")), physScreen, "layout OSD");
    if (!window) {
        return;
    }

    // Configure LayerShellQt for Wayland overlay (prevents window from appearing in taskbar)
    if (auto* layerWindow = LayerShellQt::Window::get(window)) {
        layerWindow->setScreen(physScreen);
        layerWindow->setLayer(LayerShellQt::Window::LayerOverlay);
        layerWindow->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
        // Anchors will be set dynamically in showLayoutOsd() based on window size
        // Use screenId in scope to make it unique per virtual screen
        layerWindow->setScope(QStringLiteral("plasmazones-layout-osd-%1").arg(screenId));
        layerWindow->setExclusiveZone(-1);
    }

    auto layoutOsdConn = connect(window, SIGNAL(dismissed()), this, SLOT(hideLayoutOsd()));
    if (!layoutOsdConn) {
        qCWarning(lcOverlay) << "Failed to connect dismissed signal for layout OSD on screen" << screenId;
    }
    window->setVisible(false);
    m_layoutOsdWindows.insert(screenId, window);
    m_layoutOsdPhysScreens.insert(screenId, physScreen);
}

void OverlayService::destroyLayoutOsdWindow(const QString& screenId)
{
    if (auto* window = m_layoutOsdWindows.take(screenId)) {
        window->close();
        window->deleteLater();
    }
    m_layoutOsdPhysScreens.remove(screenId);
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
    QString actionKey = action + QLatin1String(":") + reason;
    if (actionKey == m_lastNavigationAction + QLatin1String(":") + m_lastNavigationReason
        && m_lastNavigationTime.isValid() && m_lastNavigationTime.elapsed() < 200) {
        qCDebug(lcOverlay) << "Skipping duplicate navigation OSD:" << action << reason;
        return;
    }
    m_lastNavigationAction = action;
    m_lastNavigationReason = reason;
    m_lastNavigationTime.restart();

    // Resolve target screen via ScreenManager for virtual screen support
    auto* mgr = ScreenManager::instance();
    QScreen* physScreen = mgr ? mgr->physicalQScreenFor(screenId) : nullptr;
    if (!physScreen && !screenId.isEmpty()) {
        physScreen = Utils::findScreenByIdOrName(screenId);
    }
    if (!physScreen) {
        physScreen = Utils::primaryScreen();
    }
    if (!physScreen) {
        qCWarning(lcOverlay) << "No screen available for navigation OSD";
        return;
    }

    // Use virtual screen geometry if applicable, otherwise physical
    QRect vsGeom = (mgr && !screenId.isEmpty()) ? mgr->screenGeometry(screenId) : QRect();
    const QRect navScreenGeom = vsGeom.isValid() ? vsGeom : physScreen->geometry();

    QString effectiveId = screenId.isEmpty() ? Utils::screenIdentifier(physScreen) : screenId;

    // Resolve per-screen layout (not the global m_layout which may belong to another screen)
    // Float, algorithm, rotate, and autotile-only actions don't need layout/zones
    static const QStringList noLayoutActions{QStringLiteral("float"),        QStringLiteral("algorithm"),
                                             QStringLiteral("rotate"),       QStringLiteral("focus_master"),
                                             QStringLiteral("swap_master"),  QStringLiteral("master_ratio"),
                                             QStringLiteral("master_count"), QStringLiteral("retile")};
    const bool needsLayout = !noLayoutActions.contains(action);
    Layout* screenLayout = resolveScreenLayout(effectiveId);
    if ((needsLayout && !screenLayout) || (screenLayout && screenLayout->zones().isEmpty() && needsLayout)) {
        qCDebug(lcOverlay) << "No layout or zones for navigation OSD: screen=" << effectiveId
                           << "layout=" << (screenLayout ? screenLayout->name() : QStringLiteral("null"))
                           << "zones=" << (screenLayout ? screenLayout->zones().size() : 0) << "action=" << action;
        return;
    }

    // Create window if needed
    if (!m_navigationOsdWindows.contains(effectiveId)) {
        // Only try to create if we haven't failed before (prevents log spam)
        if (!m_navigationOsdCreationFailed.value(effectiveId, false)) {
            createNavigationOsdWindow(effectiveId, physScreen, navScreenGeom);
        }
    }

    auto* window = m_navigationOsdWindows.value(effectiveId);
    if (!window) {
        // Only warn once per screen to prevent log spam
        if (!m_navigationOsdCreationFailed.value(effectiveId, false)) {
            qCWarning(lcOverlay) << "Failed to get navigation OSD window for screen=" << effectiveId;
            m_navigationOsdCreationFailed.insert(effectiveId, true);
        }
        qCDebug(lcOverlay) << "No navigation OSD window for screen=" << effectiveId;
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

    // Ensure the window is on the correct Wayland output (must come before sizing --
    // assertWindowOnScreen calls setGeometry(screen) which would override setWidth/setHeight)
    assertWindowOnScreen(window, physScreen, navScreenGeom);

    // Size and center: setWidth/setHeight AFTER assertWindowOnScreen so the final
    // QWindow geometry matches the OSD size (same pattern as sizeAndCenterOsd for LayoutOsd)
    const QRect screenGeom = navScreenGeom;
    const int osdWidth = 240; // Compact width for text
    const int osdHeight = 70; // Text message + margins
    window->setWidth(osdWidth);
    window->setHeight(osdHeight);
    const QRect physGeom = physScreen ? physScreen->geometry() : screenGeom;
    centerLayerWindowOnScreen(window, physGeom, screenGeom, osdWidth, osdHeight);

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

void OverlayService::createNavigationOsdWindow(const QString& screenId, QScreen* physScreen, const QRect& screenGeom)
{
    Q_UNUSED(screenGeom)
    if (m_navigationOsdWindows.contains(screenId)) {
        return;
    }

    auto* window = createQmlWindow(QUrl(QStringLiteral("qrc:/ui/NavigationOsd.qml")), physScreen, "navigation OSD");
    if (!window) {
        m_navigationOsdCreationFailed.insert(screenId, true);
        return;
    }

    // Configure LayerShellQt for Wayland overlay
    if (auto* layerWindow = LayerShellQt::Window::get(window)) {
        layerWindow->setScreen(physScreen);
        layerWindow->setLayer(LayerShellQt::Window::LayerOverlay);
        layerWindow->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
        layerWindow->setScope(QStringLiteral("plasmazones-navigation-osd-%1").arg(screenId));
        layerWindow->setExclusiveZone(-1);
    }

    auto navOsdConn = connect(window, SIGNAL(dismissed()), this, SLOT(hideNavigationOsd()));
    if (!navOsdConn) {
        qCWarning(lcOverlay) << "Failed to connect dismissed signal for navigation OSD on screen" << screenId;
    }
    window->setVisible(false);
    m_navigationOsdWindows.insert(screenId, window);
    m_navigationOsdPhysScreens.insert(screenId, physScreen);
    m_navigationOsdCreationFailed.remove(screenId);
}

void OverlayService::destroyNavigationOsdWindow(const QString& screenId)
{
    if (auto* window = m_navigationOsdWindows.take(screenId)) {
        window->close();
        window->deleteLater();
    }
    m_navigationOsdPhysScreens.remove(screenId);
    // Clear failed flag when destroying window
    m_navigationOsdCreationFailed.remove(screenId);
}

} // namespace PlasmaZones
