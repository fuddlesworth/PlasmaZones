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
#include "../../core/layersurface.h"

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

// Center an OSD/layer window within a screen geometry using layer surface margins.
// physScreenGeom is the full physical screen; targetGeom is the area to center within
// (same as physScreenGeom for physical screens, or a sub-region for virtual screens).
// Precondition: the window must already have a LayerSurface (created before show()).
// This function retrieves the existing LayerSurface — it does not create one.
void centerLayerWindowOnScreen(QQuickWindow* window, const QRect& physScreenGeom, const QRect& targetGeom, int osdWidth,
                               int osdHeight)
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
    // Compute center position within the target area, expressed as margins from physical screen edges
    const int targetCenterX = (targetGeom.x() - physScreenGeom.x()) + qMax(0, (targetGeom.width() - osdWidth) / 2);
    const int targetCenterY = (targetGeom.y() - physScreenGeom.y()) + qMax(0, (targetGeom.height() - osdHeight) / 2);
    const int rightMargin = qMax(0, physScreenGeom.width() - targetCenterX - osdWidth);
    const int bottomMargin = qMax(0, physScreenGeom.height() - targetCenterY - osdHeight);

    // Batch anchors + margins into a single propertiesChanged() emission
    // to avoid two applyProperties()+wl_surface_commit round-trips.
    LayerSurface::BatchGuard batch(layerSurface);
    layerSurface->setAnchors(LayerSurface::AnchorAll);
    layerSurface->setMargins(QMargins(targetCenterX, targetCenterY, rightMargin, bottomMargin));
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
    // Resolve target screen using shared helper (handles virtual IDs, fallback chain)
    QScreen* physScreen = resolveTargetScreen(screenId);
    if (!physScreen) {
        qCWarning(lcOverlay) << "No screen available for layout OSD";
        return false;
    }

    outPhysScreen = physScreen;

    // Use virtual screen geometry if applicable, otherwise physical
    screenGeom = resolveScreenGeometry(screenId);
    if (!screenGeom.isValid()) {
        screenGeom = physScreen->geometry();
    }

    QString effectiveId = screenId.isEmpty() ? Utils::screenIdentifier(physScreen) : screenId;

    if (!m_layoutOsdWindows.contains(effectiveId)) {
        createLayoutOsdWindow(effectiveId, physScreen);
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
    showLayoutOsd(layout, false, screenId);
}

void OverlayService::showLayoutOsd(Layout* layout, bool locked, const QString& screenId)
{
    if (!layout) {
        qCDebug(lcOverlay) << "No layout provided for OSD";
        return;
    }

    if (!locked && layout->zones().isEmpty()) {
        qCDebug(lcOverlay) << "Skipping OSD for empty layout=" << layout->name();
        return;
    }

    showLayoutOsdImpl(layout, screenId, locked);
}

void OverlayService::showLayoutOsdImpl(Layout* layout, const QString& screenId, bool locked)
{
    QQuickWindow* window = nullptr;
    QScreen* physScreen = nullptr;
    QRect screenGeom;
    qreal aspectRatio = 0;
    if (!prepareLayoutOsdWindow(window, physScreen, screenGeom, aspectRatio, screenId)) {
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
    sizeAndCenterOsd(window, physScreen, screenGeom, layoutAR);
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
    QScreen* physScreen = nullptr;
    QRect screenGeom;
    qreal aspectRatio = 0;
    if (!prepareLayoutOsdWindow(window, physScreen, screenGeom, aspectRatio, screenId)) {
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

    sizeAndCenterOsd(window, physScreen, screenGeom, layoutAR);
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
    const QStringList osdScreenIds = m_layoutOsdWindows.keys();
    for (const QString& sid : osdScreenIds) {
        destroyLayoutOsdWindow(sid);
    }
}

void OverlayService::warmUpLayoutOsd()
{
    auto* mgr = ScreenManager::instance();
    const QStringList effectiveIds = mgr ? mgr->effectiveScreenIds() : QStringList();

    if (!effectiveIds.isEmpty()) {
        // Warm up one OSD per effective screen ID (including all virtual screens).
        // Each virtual screen may receive its own showLayoutOsd call, so it needs
        // a pre-warmed window keyed by its own ID. Without this, only the first
        // VS ID per physical screen would have a warm window, and showLayoutOsd
        // for subsequent VS IDs would hit cold QML compilation (~100-300ms).
        for (const QString& sid : effectiveIds) {
            if (!m_layoutOsdWindows.contains(sid)) {
                QScreen* physScreen = mgr->physicalQScreenFor(sid);
                if (physScreen) {
                    QRect geom = mgr->screenGeometry(sid);
                    if (!geom.isValid()) {
                        geom = physScreen->geometry();
                    }
                    createLayoutOsdWindow(sid, physScreen);
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
                createLayoutOsdWindow(sid, screen);
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
                            createLayoutOsdWindow(vsId, screen);
                        }
                    }
                }
            } else {
                if (!m_layoutOsdWindows.contains(physId)) {
                    QRect geom = mgr2 ? mgr2->screenGeometry(physId) : screen->geometry();
                    if (!geom.isValid()) {
                        geom = screen->geometry();
                    }
                    createLayoutOsdWindow(physId, screen);
                }
            }
            // Warm up navigation OSD for each effective screen on this physical
            // monitor (virtual screens when configured, physical otherwise),
            // matching the layout OSD logic above.
            if (mgr2 && mgr2->hasVirtualScreens(physId)) {
                for (const QString& vsId : mgr2->virtualScreenIdsFor(physId)) {
                    if (!m_navigationOsdWindows.contains(vsId)) {
                        createNavigationOsdWindow(vsId, screen);
                    }
                }
            } else {
                if (!m_navigationOsdWindows.contains(physId)) {
                    createNavigationOsdWindow(physId, screen);
                }
            }
        });
        m_screenAddedConnected = true;
    }
}

void OverlayService::warmUpNavigationOsd()
{
    auto* mgr = ScreenManager::instance();
    if (mgr) {
        // Iterate effective screen IDs so virtual screens get their own OSD
        // windows, matching the warmUpLayoutOsd() pattern.
        const QStringList effectiveIds = mgr->effectiveScreenIds();
        for (const QString& sid : effectiveIds) {
            if (!m_navigationOsdWindows.contains(sid)) {
                QScreen* physScreen = ScreenManager::resolvePhysicalScreen(sid);
                if (physScreen) {
                    createNavigationOsdWindow(sid, physScreen);
                }
            }
        }
        qCInfo(lcOverlay) << "Pre-warmed Navigation OSD windows for" << effectiveIds.size() << "effective screens";
    } else {
        const auto screens = QGuiApplication::screens();
        for (QScreen* screen : screens) {
            QString sid = Utils::screenIdentifier(screen);
            if (!m_navigationOsdWindows.contains(sid)) {
                createNavigationOsdWindow(sid, screen);
            }
        }
        qCInfo(lcOverlay) << "Pre-warmed Navigation OSD windows for" << screens.size() << "screens";
    }
}

void OverlayService::createLayoutOsdWindow(const QString& screenId, QScreen* physScreen)
{
    if (m_layoutOsdWindows.contains(screenId)) {
        return;
    }

    auto* window = createQmlWindow(QUrl(QStringLiteral("qrc:/ui/LayoutOsd.qml")), physScreen, "layout OSD");
    if (!window) {
        return;
    }

    // Configure layer surface for Wayland overlay (prevents window from appearing in taskbar)
    // Anchors will be set dynamically in showLayoutOsd() based on window size
    // Use screenId in scope to make it unique per virtual screen
    if (!configureLayerSurface(window, physScreen, LayerSurface::LayerOverlay, LayerSurface::KeyboardInteractivityNone,
                               QStringLiteral("plasmazones-layout-osd-%1-%2").arg(screenId).arg(++m_scopeGeneration))) {
        qCWarning(lcOverlay) << "Failed to configure layer surface for layout OSD on" << screenId;
        window->deleteLater();
        return;
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
    destroyManagedWindow(m_layoutOsdWindows, m_layoutOsdPhysScreens, screenId);
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

    // Deduplicate: Skip if same action+reason+screen within 200ms (prevents duplicate from Qt signal + D-Bus signal)
    const QString actionKey = action + QLatin1Char(':') + reason;
    if (actionKey == m_lastNavigationActionKey && screenId == m_lastNavigationScreenId && m_lastNavigationTime.isValid()
        && m_lastNavigationTime.elapsed() < 200) {
        qCDebug(lcOverlay) << "Skipping duplicate navigation OSD:" << action << reason;
        return;
    }
    m_lastNavigationActionKey = actionKey;
    m_lastNavigationScreenId = screenId;
    m_lastNavigationTime.restart();

    // Resolve target screen using shared helper (handles virtual IDs, fallback chain)
    QScreen* physScreen = resolveTargetScreen(screenId);
    if (!physScreen) {
        qCWarning(lcOverlay) << "No screen available for navigation OSD";
        return;
    }

    // Use virtual screen geometry if applicable, otherwise physical
    QRect navScreenGeom = resolveScreenGeometry(screenId);
    if (!navScreenGeom.isValid()) {
        navScreenGeom = physScreen->geometry();
    }

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

    // Reuse existing window for this screen (create only if not in map).
    // The window stays alive and visible across rapid navigation calls —
    // QML show() resets the animation and restarts the dismiss timer each time.
    // Cleanup happens when the dismiss timer expires: dismissed() signal →
    // hideNavigationOsd() slot → destroyNavigationOsdWindow(). This matches
    // the layout OSD pattern and avoids Vulkan surface create/destroy churn
    // that causes resource exhaustion and daemon freezes during rapid input.
    if (!m_navigationOsdWindows.contains(effectiveId)) {
        // Only try to create if we haven't failed before (prevents log spam)
        if (!m_navigationOsdCreationFailed.value(effectiveId, false)) {
            createNavigationOsdWindow(effectiveId, physScreen);
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

    // Ensure the window is on the correct Wayland output (must come before sizing —
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
    // Clear dedup state so the next show isn't spuriously suppressed within 200ms
    m_lastNavigationActionKey.clear();
    m_lastNavigationScreenId.clear();
    // Fallback: no sender (direct call) — destroy all
    const QStringList navScreenIds = m_navigationOsdWindows.keys();
    for (const QString& sid : navScreenIds) {
        destroyNavigationOsdWindow(sid);
    }
}

void OverlayService::createNavigationOsdWindow(const QString& screenId, QScreen* physScreen)
{
    if (m_navigationOsdWindows.contains(screenId)) {
        return;
    }

    auto* window = createQmlWindow(QUrl(QStringLiteral("qrc:/ui/NavigationOsd.qml")), physScreen, "navigation OSD");
    if (!window) {
        m_navigationOsdCreationFailed.insert(screenId, true);
        return;
    }

    // Configure layer surface for Wayland overlay
    if (!configureLayerSurface(
            window, physScreen, LayerSurface::LayerOverlay, LayerSurface::KeyboardInteractivityNone,
            QStringLiteral("plasmazones-navigation-osd-%1-%2").arg(screenId).arg(++m_scopeGeneration))) {
        qCWarning(lcOverlay) << "Failed to configure layer surface for navigation OSD on" << screenId;
        m_navigationOsdCreationFailed.insert(screenId, true);
        window->deleteLater();
        return;
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
    destroyManagedWindow(m_navigationOsdWindows, m_navigationOsdPhysScreens, screenId);
    m_navigationOsdCreationFailed.remove(screenId);
}

} // namespace PlasmaZones
