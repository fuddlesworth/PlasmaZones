// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "internal.h"
#include "../overlayservice.h"
#include "../../core/logging.h"
#include "../../core/layout.h"
#include "../../core/layoutmanager.h"
#include "../../core/zone.h"
#include "../../core/layoututils.h"
#include "../../core/geometryutils.h"
#include "../../core/screenmanager.h"
#include "../../core/utils.h"
#include "../../core/zoneselectorlayout.h"
#include "../config/configdefaults.h"
#include <QScreen>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQmlEngine>
#include <LayerShellQt/Window>

namespace PlasmaZones {

void OverlayService::showZoneSelector(const QString& targetScreenId)
{
    if (m_zoneSelectorVisible) {
        return;
    }

    // Check if zone selector is enabled in settings
    if (m_settings && !m_settings->zoneSelectorEnabled()) {
        return;
    }

    m_zoneSelectorVisible = true;

    // Resolve target screen from screenId (supports virtual screen IDs)
    auto* mgr = ScreenManager::instance();
    QScreen* targetScreen = nullptr;
    if (!targetScreenId.isEmpty()) {
        targetScreen = mgr ? mgr->physicalQScreenFor(targetScreenId) : Utils::findScreenByIdOrName(targetScreenId);
    }

    const QStringList effectiveIds = mgr ? mgr->effectiveScreenIds() : QStringList();

    if (mgr && !effectiveIds.isEmpty()) {
        for (const QString& screenId : effectiveIds) {
            QScreen* physScreen = mgr->physicalQScreenFor(screenId);
            if (!physScreen) {
                continue;
            }
            // Only show on the target screen (nullptr = all screens)
            if (targetScreen && physScreen != targetScreen && screenId != targetScreenId) {
                continue;
            }
            // Skip monitors where PlasmaZones is disabled
            if (m_settings && m_settings->isMonitorDisabled(screenId)) {
                continue;
            }
            // Skip autotile-managed screens (zone selector is for manual zone selection)
            if (m_excludedScreens.contains(screenId)) {
                continue;
            }
            const QRect geom = mgr->screenGeometry(screenId);
            if (!m_zoneSelectorWindows.contains(screenId)) {
                createZoneSelectorWindow(screenId, physScreen, geom.isValid() ? geom : physScreen->geometry());
            }
            if (auto* window = m_zoneSelectorWindows.value(screenId)) {
                assertWindowOnScreen(window, physScreen, geom.isValid() ? geom : physScreen->geometry());
                updateZoneSelectorWindow(screenId);
                window->show();
            } else {
                qCWarning(lcOverlay) << "No window found for screen" << screenId;
            }
        }
    } else {
        // Fallback: no ScreenManager
        for (auto* screen : Utils::allScreens()) {
            if (targetScreen && screen != targetScreen) {
                continue;
            }
            QString screenId = Utils::screenIdentifier(screen);
            if (m_settings && m_settings->isMonitorDisabled(screenId)) {
                continue;
            }
            if (m_excludedScreens.contains(screenId)) {
                continue;
            }
            if (!m_zoneSelectorWindows.contains(screenId)) {
                createZoneSelectorWindow(screenId, screen, screen->geometry());
            }
            if (auto* window = m_zoneSelectorWindows.value(screenId)) {
                assertWindowOnScreen(window, screen);
                updateZoneSelectorWindow(screenId);
                window->show();
            } else {
                qCWarning(lcOverlay) << "No window found for screen" << screen->name();
            }
        }
    }

    Q_EMIT zoneSelectorVisibilityChanged(true);
}

void OverlayService::hideZoneSelector()
{
    if (!m_zoneSelectorVisible) {
        return;
    }

    m_zoneSelectorVisible = false;

    // Note: Don't clear selected zone here - we need it for snapping when drag ends
    // The selected zone will be cleared after the snap is processed

    for (auto* window : std::as_const(m_zoneSelectorWindows)) {
        if (window) {
            window->hide();
        }
    }

    Q_EMIT zoneSelectorVisibilityChanged(false);
}

void OverlayService::updateSelectorPosition(int cursorX, int cursorY)
{
    if (!m_zoneSelectorVisible) {
        return;
    }

    // Find which screen the cursor is on
    QScreen* screen = Utils::findScreenAtPosition(cursorX, cursorY);

    if (!screen) {
        return;
    }

    // Update the zone selector window with cursor position for hover effects
    // Resolve to effective (virtual) screen ID if applicable
    auto* mgr = ScreenManager::instance();
    QString cursorScreenId;
    if (mgr) {
        cursorScreenId = mgr->effectiveScreenAt(QPoint(cursorX, cursorY));
    }
    if (cursorScreenId.isEmpty()) {
        cursorScreenId = Utils::screenIdentifier(screen);
    }
    if (auto* window = m_zoneSelectorWindows.value(cursorScreenId)) {
        // With exclusiveZone=-1, the window is positioned deterministically
        // and mapFromGlobal gives us accurate local coordinates without compensation
        const QPoint localPos = window->mapFromGlobal(QPoint(cursorX, cursorY));
        int localX = localPos.x();
        int localY = localPos.y();

        window->setProperty("cursorX", localX);
        window->setProperty("cursorY", localY);

        // Get layouts from QML window
        QVariantList layouts = window->property("layouts").toList();
        if (layouts.isEmpty()) {
            return;
        }

        const int layoutCount = layouts.size();
        const ZoneSelectorConfig selectorConfig =
            m_settings ? m_settings->resolvedZoneSelectorConfig(cursorScreenId) : defaultZoneSelectorConfig();
        const ZoneSelectorLayout layout = computeZoneSelectorLayout(selectorConfig, screen, layoutCount);

        // Get grid position from QML - it knows exactly where the content is rendered
        int contentGridX = 0;
        int contentGridY = 0;

        if (auto* contentRoot = window->contentItem()) {
            if (auto* gridItem = findQmlItemByName(contentRoot, QStringLiteral("zoneSelectorContentGrid"))) {
                QRectF gridRect =
                    gridItem->mapRectToItem(contentRoot, QRectF(0, 0, gridItem->width(), gridItem->height()));
                contentGridX = qRound(gridRect.x());
                contentGridY = qRound(gridRect.y());
            }
        }

        // Check each layout indicator
        for (int i = 0; i < layouts.size(); ++i) {
            int row = (layout.columns > 0) ? (i / layout.columns) : 0;
            int col = (layout.columns > 0) ? (i % layout.columns) : 0;
            // Cell origin includes card chrome; preview is offset by cardSidePadding horizontally
            // and cardTopMargin vertically (matches Kirigami.Units.gridUnit in LayoutCard.qml)
            int indicatorX = contentGridX + col * (layout.cellWidth + layout.indicatorSpacing) + layout.cardSidePadding;
            int indicatorY = contentGridY + row * (layout.cellHeight + layout.indicatorSpacing) + layout.cardTopMargin;

            // Check if cursor is over this indicator
            if (localX >= indicatorX && localX < indicatorX + layout.indicatorWidth && localY >= indicatorY
                && localY < indicatorY + layout.indicatorHeight) {
                QVariantMap layoutMap = layouts[i].toMap();
                QString layoutId = layoutMap[QStringLiteral("id")].toString();

                // Skip non-active layouts when screen is locked (either mode)
                if (m_settings && m_layoutManager) {
                    int curDesktop = m_layoutManager->currentVirtualDesktop();
                    QString curActivity = m_layoutManager->currentActivity();
                    bool locked =
                        m_settings->isContextLocked(QStringLiteral("0:") + cursorScreenId, curDesktop, curActivity)
                        || m_settings->isContextLocked(QStringLiteral("1:") + cursorScreenId, curDesktop, curActivity);
                    if (locked) {
                        // Only allow zone selection from the active layout
                        Layout* activeLayout = m_layoutManager->resolveLayoutForScreen(cursorScreenId);
                        if (activeLayout && layoutId != activeLayout->id().toString()) {
                            continue; // Skip this non-active layout entirely
                        }
                    }
                }

                // Per-zone hit testing
                QVariantList zones = layoutMap[QStringLiteral("zones")].toList();
                int scaledPadding = window->property("scaledPadding").toInt();
                if (scaledPadding <= 0)
                    scaledPadding = 1;
                constexpr int minZoneSize = 8;

                for (int z = 0; z < zones.size(); ++z) {
                    QVariantMap zoneMap = zones[z].toMap();
                    QVariantMap relGeo = zoneMap[QStringLiteral("relativeGeometry")].toMap();
                    qreal rx = relGeo[QStringLiteral("x")].toReal();
                    qreal ry = relGeo[QStringLiteral("y")].toReal();
                    qreal rw = relGeo[QStringLiteral("width")].toReal();
                    qreal rh = relGeo[QStringLiteral("height")].toReal();

                    // Calculate zone rectangle exactly as QML does
                    int zoneX = indicatorX + static_cast<int>(rx * layout.indicatorWidth) + scaledPadding;
                    int zoneY = indicatorY + static_cast<int>(ry * layout.indicatorHeight) + scaledPadding;
                    int zoneW = std::max(minZoneSize, static_cast<int>(rw * layout.indicatorWidth) - scaledPadding * 2);
                    int zoneH =
                        std::max(minZoneSize, static_cast<int>(rh * layout.indicatorHeight) - scaledPadding * 2);

                    if (localX >= zoneX && localX < zoneX + zoneW && localY >= zoneY && localY < zoneY + zoneH) {
                        // Found the zone - update selection
                        if (m_selectedLayoutId != layoutId || m_selectedZoneIndex != z) {
                            m_selectedLayoutId = layoutId;
                            m_selectedZoneIndex = z;
                            m_selectedZoneRelGeo = QRectF(rx, ry, rw, rh);
                            window->setProperty("selectedLayoutId", layoutId);
                            window->setProperty("selectedZoneIndex", z);
                        }
                        return;
                    }
                }
                // Cursor is over layout indicator but not on a specific zone
                // Clear selection if we had one in a different layout
                if (!m_selectedLayoutId.isEmpty() && m_selectedLayoutId != layoutId) {
                    m_selectedLayoutId.clear();
                    m_selectedZoneIndex = -1;
                    m_selectedZoneRelGeo = QRectF();
                    window->setProperty("selectedLayoutId", QString());
                    window->setProperty("selectedZoneIndex", -1);
                }
                return;
            }
        }

        // Cursor is not over any layout indicator - clear selection
        if (!m_selectedLayoutId.isEmpty()) {
            m_selectedLayoutId.clear();
            m_selectedZoneIndex = -1;
            m_selectedZoneRelGeo = QRectF();
            window->setProperty("selectedLayoutId", QString());
            window->setProperty("selectedZoneIndex", -1);
        }
    }
}

void OverlayService::createZoneSelectorWindow(const QString& screenId, QScreen* physScreen, const QRect& geom)
{
    if (m_zoneSelectorWindows.contains(screenId)) {
        return;
    }

    auto* window = createQmlWindow(QUrl(QStringLiteral("qrc:/ui/ZoneSelectorWindow.qml")), physScreen, "zone selector");
    if (!window) {
        return;
    }

    const QRect screenGeom = geom.isValid() ? geom : physScreen->geometry();

    // Build resolved per-screen config
    const ZoneSelectorConfig config =
        m_settings ? m_settings->resolvedZoneSelectorConfig(screenId) : defaultZoneSelectorConfig();
    const auto pos = static_cast<ZoneSelectorPosition>(config.position);

    // Configure LayerShellQt for zone selector (LayerTop for pointer input)
    if (auto* layerWindow = LayerShellQt::Window::get(window)) {
        layerWindow->setScreen(physScreen);
        layerWindow->setLayer(LayerShellQt::Window::LayerTop);
        layerWindow->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);

        layerWindow->setAnchors(getAnchorsForPosition(pos));
        layerWindow->setExclusiveZone(-1);
        layerWindow->setScope(QStringLiteral("plasmazones-selector-%1").arg(screenId));
    }

    // Set screen properties for layout preview scaling
    qreal aspectRatio =
        (screenGeom.height() > 0) ? static_cast<qreal>(screenGeom.width()) / screenGeom.height() : (16.0 / 9.0);
    writeQmlProperty(window, QStringLiteral("screenAspectRatio"), aspectRatio);
    writeQmlProperty(window, QStringLiteral("screenWidth"), screenGeom.width());

    // Pass zone appearance settings for scaled preview (global settings)
    if (m_settings) {
        writeQmlProperty(window, QStringLiteral("zonePadding"), m_settings->zonePadding());
        writeQmlProperty(window, QStringLiteral("zoneBorderWidth"), m_settings->borderWidth());
        writeQmlProperty(window, QStringLiteral("zoneBorderRadius"), m_settings->borderRadius());
    }
    // Pass resolved per-screen config values to QML
    writeQmlProperty(window, QStringLiteral("selectorPosition"), config.position);
    writeQmlProperty(window, QStringLiteral("selectorLayoutMode"), config.layoutMode);
    writeQmlProperty(window, QStringLiteral("selectorGridColumns"), config.gridColumns);
    writeQmlProperty(window, QStringLiteral("previewWidth"), config.previewWidth);
    writeQmlProperty(window, QStringLiteral("previewHeight"), config.previewHeight);
    writeQmlProperty(window, QStringLiteral("previewLockAspect"), config.previewLockAspect);

    // Initial layout is applied by updateZoneSelectorWindow() which is always
    // called immediately after createZoneSelectorWindow() in showZoneSelector().

    window->setVisible(false);
    auto conn = connect(window, SIGNAL(zoneSelected(QString, int, QVariant)), this,
                        SLOT(onZoneSelected(QString, int, QVariant)));
    if (!conn) {
        qCWarning(lcOverlay) << "Failed to connect zoneSelected signal for screen" << screenId
                             << "- zone selector layout switching will not work";
    }
    m_zoneSelectorWindows.insert(screenId, window);
    m_zoneSelectorPhysScreens.insert(screenId, physScreen);
}

void OverlayService::destroyZoneSelectorWindow(const QString& screenId)
{
    if (auto* window = m_zoneSelectorWindows.take(screenId)) {
        window->close();
        window->deleteLater();
    }
    m_zoneSelectorPhysScreens.remove(screenId);
}

bool OverlayService::hasSelectedZone() const
{
    return !m_selectedLayoutId.isEmpty() && m_selectedZoneIndex >= 0;
}

void OverlayService::clearSelectedZone()
{
    m_selectedLayoutId.clear();
    m_selectedZoneIndex = -1;
    m_selectedZoneRelGeo = QRectF();
}

QRect OverlayService::getSelectedZoneGeometry(QScreen* screen) const
{
    if (!hasSelectedZone() || !screen) {
        return QRect();
    }
    // Delegate to screenId overload for virtual-screen-aware geometry
    return getSelectedZoneGeometry(Utils::screenIdentifier(screen));
}

QRect OverlayService::getSelectedZoneGeometry(const QString& screenId) const
{
    if (!hasSelectedZone()) {
        return QRect();
    }

    auto* mgr = ScreenManager::instance();
    QScreen* physScreen = mgr ? mgr->physicalQScreenFor(screenId) : Utils::findScreenByIdOrName(screenId);

    // Primary path: use layout/zone geometry pipeline with virtual screen bounds
    if (m_layoutManager && !m_selectedLayoutId.isEmpty()) {
        Layout* selectedLayout = m_layoutManager->layoutById(QUuid::fromString(m_selectedLayoutId));
        if (selectedLayout && m_selectedZoneIndex >= 0
            && m_selectedZoneIndex < static_cast<int>(selectedLayout->zones().size())) {
            Zone* zone = selectedLayout->zones().at(m_selectedZoneIndex);
            if (zone) {
                int zonePadding = GeometryUtils::getEffectiveZonePadding(selectedLayout, m_settings, screenId);
                EdgeGaps outerGaps = GeometryUtils::getEffectiveOuterGaps(selectedLayout, m_settings, screenId);
                bool useAvail = !(selectedLayout && selectedLayout->useFullScreenGeometry());

                // Use virtual screen geometry when available
                if (mgr) {
                    QRect vsGeom = mgr->screenGeometry(screenId);
                    QRect vsAvailGeom = mgr->screenAvailableGeometry(screenId);
                    if (vsGeom.isValid()) {
                        QRectF geom = GeometryUtils::getZoneGeometryWithGaps(
                            zone, vsGeom, vsAvailGeom.isValid() ? vsAvailGeom : vsGeom, zonePadding, outerGaps,
                            useAvail);
                        return GeometryUtils::snapToRect(geom);
                    }
                }

                // Fallback to physical screen
                if (physScreen) {
                    QRectF geom =
                        GeometryUtils::getZoneGeometryWithGaps(zone, physScreen, zonePadding, outerGaps, useAvail);
                    return GeometryUtils::snapToRect(geom);
                }
            }
        }
    }

    // Fallback: manual calculation using relative geometry
    QRect areaGeom;
    if (mgr) {
        QRect vsAvailGeom = mgr->screenAvailableGeometry(screenId);
        if (vsAvailGeom.isValid()) {
            areaGeom = vsAvailGeom;
        }
    }
    if (!areaGeom.isValid() && physScreen) {
        areaGeom = ScreenManager::actualAvailableGeometry(physScreen);
    }
    if (!areaGeom.isValid()) {
        return QRect();
    }

    QRectF geom(areaGeom.x() + m_selectedZoneRelGeo.x() * areaGeom.width(),
                areaGeom.y() + m_selectedZoneRelGeo.y() * areaGeom.height(),
                m_selectedZoneRelGeo.width() * areaGeom.width(), m_selectedZoneRelGeo.height() * areaGeom.height());
    return GeometryUtils::snapToRect(geom);
}

void OverlayService::onZoneSelected(const QString& layoutId, int zoneIndex, const QVariant& relativeGeometry)
{
    m_selectedLayoutId = layoutId;
    m_selectedZoneIndex = zoneIndex;

    // Convert QVariant to QVariantMap and extract relative geometry
    QVariantMap relGeoMap = relativeGeometry.toMap();
    qreal x = relGeoMap.value(QStringLiteral("x"), 0.0).toReal();
    qreal y = relGeoMap.value(QStringLiteral("y"), 0.0).toReal();
    qreal width = relGeoMap.value(QStringLiteral("width"), 0.0).toReal();
    qreal height = relGeoMap.value(QStringLiteral("height"), 0.0).toReal();
    m_selectedZoneRelGeo = QRectF(x, y, width, height);

    // Determine which screen the zone selector is on from the sender window
    // Primary: look up in our window-to-screen map (authoritative assignment)
    // Fallback: use Qt's screen assignment on the window itself
    QString screenId;
    auto* senderWindow = qobject_cast<QQuickWindow*>(sender());
    if (senderWindow) {
        for (auto it = m_zoneSelectorWindows.constBegin(); it != m_zoneSelectorWindows.constEnd(); ++it) {
            if (it.value() == senderWindow) {
                screenId = it.key();
                break;
            }
        }
        if (screenId.isEmpty() && senderWindow->screen()) {
            screenId = Utils::screenIdentifier(senderWindow->screen());
        }
    }

    // Route to the correct signal based on whether this is an autotile algorithm or manual layout
    if (LayoutId::isAutotile(layoutId)) {
        const QString algoId = LayoutId::extractAlgorithmId(layoutId);
        qCInfo(lcOverlay) << "Zone selector: autotile algorithm selected, algoId=" << algoId << "screen=" << screenId;
        Q_EMIT autotileLayoutSelected(algoId, screenId);
    } else {
        qCInfo(lcOverlay) << "Zone selector: layout selected, layoutId=" << layoutId << "screen=" << screenId;
        Q_EMIT manualLayoutSelected(layoutId, screenId);
    }
}

void OverlayService::scrollZoneSelector(int angleDeltaY)
{
    if (!m_zoneSelectorVisible) {
        return;
    }
    for (auto* window : std::as_const(m_zoneSelectorWindows)) {
        if (window) {
            QMetaObject::invokeMethod(window, "applyScrollDelta", Q_ARG(QVariant, angleDeltaY));
        }
    }
}

} // namespace PlasmaZones
