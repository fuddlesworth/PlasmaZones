// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../EditorController.h"
#include "../services/ILayoutService.h"
#include "../services/ZoneManager.h"
#include "../undo/UndoController.h"
#include "../helpers/ShaderDbusQueries.h"
#include "../../core/constants.h"
#include "../../core/layoututils.h"
#include "../../core/shaderregistry.h"
#include "../../core/logging.h"
#include "../../core/utils.h"

#include "pz_i18n.h"
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>
#include <QUuid>

namespace PlasmaZones {

// ---------------------------------------------------------------------------
// Group 1 - Screen targeting
// ---------------------------------------------------------------------------

void EditorController::setTargetScreen(const QString& screenName)
{
    if (m_targetScreen != screenName) {
        // Check for unsaved changes before switching screens
        if (m_hasUnsavedChanges) {
            // For now, just warn - in future could prompt user
            qCWarning(lcEditor) << "Switching screens with unsaved changes";
        }

        QString previousLayout = m_layoutId;
        m_targetScreen = screenName;
        Q_EMIT targetScreenChanged();
        Q_EMIT targetScreenSizeChanged();
        m_zoneManager->setReferenceScreenSize(targetScreenSize());

        // Load the layout assigned to this screen
        if (!screenName.isEmpty() && m_layoutService) {
            QString layoutId = m_layoutService->getLayoutIdForScreen(screenName);
            qCDebug(lcEditor) << "setTargetScreen:" << screenName << "daemon returned layoutId:" << layoutId
                              << "current layoutId:" << previousLayout;
            if (!layoutId.isEmpty()) {
                // Load the assigned layout
                loadLayout(layoutId);
            } else {
                // No layout assigned to this screen - create a new one
                qCInfo(lcEditor) << "No layout assigned to screen" << screenName << "- creating new layout";
                createNewLayout();
            }
        }
    }
}

void EditorController::showFullScreenOnTargetScreen(QQuickWindow* window)
{
    if (!window) {
        return;
    }

    if (!m_targetScreen.isEmpty()) {
        const auto screens = QGuiApplication::screens();
        for (auto* screen : screens) {
            if (Utils::screenIdentifier(screen) == m_targetScreen || screen->name() == m_targetScreen) {
                // Already on the correct screen — nothing to do
                if (window->screen() == screen && window->isVisible()) {
                    return;
                }

                qCDebug(lcEditor) << "Setting editor window to screen:" << screen->name()
                                  << "geometry:" << screen->geometry();

                // On Wayland, the wl_output for an xdg-shell surface is bound at
                // surface creation time. setScreen()/setGeometry() cannot move an
                // already-mapped surface to a different output. To switch screens we
                // must destroy the native window (wl_surface) and let Qt recreate it
                // so the new surface gets mapped to the correct output.
                // When the window is already visible (mid-session screen switch), defer
                // the destroy+recreate to avoid tearing down the surface inside the
                // current render frame or signal handler (QTBUG-88997).
                if (window->isVisible()) {
                    QPointer<QQuickWindow> safeWindow(window);
                    QScreen* targetScreen = screen;
                    QTimer::singleShot(0, window, [safeWindow, targetScreen]() {
                        if (!safeWindow || !targetScreen) {
                            return;
                        }
                        safeWindow->destroy();
                        safeWindow->setScreen(targetScreen);
                        safeWindow->setGeometry(targetScreen->geometry());
                        safeWindow->showFullScreen();
                    });
                    return;
                }

                window->setScreen(screen);
                window->setGeometry(screen->geometry());
                break;
            }
        }
    }

    window->showFullScreen();
}

void EditorController::setTargetScreenDirect(const QString& screenName)
{
    // Sets the target screen without loading a layout - used during initialization
    // when a layout is explicitly specified via command line
    if (m_targetScreen != screenName) {
        m_targetScreen = screenName;
        Q_EMIT targetScreenChanged();
        Q_EMIT targetScreenSizeChanged();
        m_zoneManager->setReferenceScreenSize(targetScreenSize());
    }
}

// ---------------------------------------------------------------------------
// Group 2 - Layout lifecycle
// ---------------------------------------------------------------------------

/**
 * @brief Creates a new empty layout
 *
 * Generates a new layout ID and initializes an empty layout.
 * Emits signals to notify QML of the new layout state.
 */
void EditorController::createNewLayout()
{
    m_layoutId = QUuid::createUuid().toString();
    m_layoutName = PzI18n::tr("New Layout");
    if (m_zoneManager) {
        m_zoneManager->clearAllZones();
    }
    m_selectedZoneId.clear();
    m_selectedZoneIds.clear();
    m_isNewLayout = true;
    m_hasUnsavedChanges = true;

    // Reset shader state
    m_currentShaderId.clear();
    m_currentShaderParams.clear();
    m_cachedShaderParameters.clear();

    // Reset per-layout gap overrides (-1 = use global)
    m_zonePadding = -1;
    m_outerGap = -1;
    m_usePerSideOuterGap = false;
    m_outerGapTop = -1;
    m_outerGapBottom = -1;
    m_outerGapLeft = -1;
    m_outerGapRight = -1;
    m_overlayDisplayMode = -1;
    m_useFullScreenGeometry = false;
    m_aspectRatioClass = 0;

    // Refresh available shaders from daemon
    refreshAvailableShaders();

    ++m_zonesVersion;
    Q_EMIT layoutIdChanged();
    Q_EMIT layoutNameChanged();
    Q_EMIT zonesChanged();
    Q_EMIT selectedZoneIdChanged();
    Q_EMIT selectedZoneIdsChanged();
    Q_EMIT hasUnsavedChangesChanged();
    Q_EMIT currentShaderIdChanged();
    Q_EMIT currentShaderParamsChanged();
    Q_EMIT currentShaderParametersChanged();
    Q_EMIT zonePaddingChanged();
    Q_EMIT outerGapChanged();
    Q_EMIT overlayDisplayModeChanged();
    Q_EMIT useFullScreenGeometryChanged();
    Q_EMIT aspectRatioClassChanged();
}

void EditorController::loadLayout(const QString& layoutId)
{
    if (layoutId.isEmpty()) {
        Q_EMIT layoutLoadFailed(PzI18n::tr("Layout ID cannot be empty"));
        return;
    }

    if (!m_layoutService) {
        Q_EMIT layoutLoadFailed(PzI18n::tr("Layout service not initialized"));
        return;
    }

    QString jsonLayout = m_layoutService->loadLayout(layoutId);
    if (jsonLayout.isEmpty()) {
        // Error signal already emitted by service
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(jsonLayout.toUtf8());
    if (doc.isNull() || !doc.isObject()) {
        Q_EMIT layoutLoadFailed(PzI18n::tr("Invalid layout data format"));
        qCWarning(lcEditor) << "Invalid JSON for layout" << layoutId;
        return;
    }

    QJsonObject layoutObj = doc.object();
    m_layoutId = layoutObj[QLatin1String(JsonKeys::Id)].toString();
    m_layoutName = layoutObj[QLatin1String(JsonKeys::Name)].toString();

    // Parse zones
    QVariantList zones;
    QJsonArray zonesArray = layoutObj[QLatin1String(JsonKeys::Zones)].toArray();
    for (const QJsonValue& zoneVal : zonesArray) {
        QJsonObject zoneObj = zoneVal.toObject();
        QVariantMap zone;

        zone[JsonKeys::Id] = zoneObj[QLatin1String(JsonKeys::Id)].toString();
        zone[JsonKeys::Name] = zoneObj[QLatin1String(JsonKeys::Name)].toString();
        zone[JsonKeys::ZoneNumber] = zoneObj[QLatin1String(JsonKeys::ZoneNumber)].toInt();

        QJsonObject relGeo = zoneObj[QLatin1String(JsonKeys::RelativeGeometry)].toObject();
        zone[JsonKeys::X] = relGeo[QLatin1String(JsonKeys::X)].toDouble();
        zone[JsonKeys::Y] = relGeo[QLatin1String(JsonKeys::Y)].toDouble();
        zone[JsonKeys::Width] = relGeo[QLatin1String(JsonKeys::Width)].toDouble();
        zone[JsonKeys::Height] = relGeo[QLatin1String(JsonKeys::Height)].toDouble();

        // Geometry mode (default: Relative = 0)
        int geoMode = zoneObj.contains(QLatin1String(JsonKeys::GeometryMode))
            ? zoneObj[QLatin1String(JsonKeys::GeometryMode)].toInt()
            : 0;
        zone[JsonKeys::GeometryMode] = geoMode;

        if (geoMode == static_cast<int>(ZoneGeometryMode::Fixed)
            && zoneObj.contains(QLatin1String(JsonKeys::FixedGeometry))) {
            QJsonObject fixedGeo = zoneObj[QLatin1String(JsonKeys::FixedGeometry)].toObject();
            zone[JsonKeys::FixedX] = fixedGeo[QLatin1String(JsonKeys::X)].toDouble();
            zone[JsonKeys::FixedY] = fixedGeo[QLatin1String(JsonKeys::Y)].toDouble();
            zone[JsonKeys::FixedWidth] = fixedGeo[QLatin1String(JsonKeys::Width)].toDouble();
            zone[JsonKeys::FixedHeight] = fixedGeo[QLatin1String(JsonKeys::Height)].toDouble();
        }

        // Appearance
        QJsonObject appearance = zoneObj[QLatin1String(JsonKeys::Appearance)].toObject();
        zone[JsonKeys::HighlightColor] = appearance[QLatin1String(JsonKeys::HighlightColor)].toString();
        zone[JsonKeys::InactiveColor] = appearance[QLatin1String(JsonKeys::InactiveColor)].toString();
        zone[JsonKeys::BorderColor] = appearance[QLatin1String(JsonKeys::BorderColor)].toString();
        // Load all appearance properties with defaults if missing
        zone[JsonKeys::ActiveOpacity] = appearance.contains(QLatin1String(JsonKeys::ActiveOpacity))
            ? appearance[QLatin1String(JsonKeys::ActiveOpacity)].toDouble()
            : Defaults::Opacity;
        zone[JsonKeys::InactiveOpacity] = appearance.contains(QLatin1String(JsonKeys::InactiveOpacity))
            ? appearance[QLatin1String(JsonKeys::InactiveOpacity)].toDouble()
            : Defaults::InactiveOpacity;
        zone[JsonKeys::BorderWidth] = appearance.contains(QLatin1String(JsonKeys::BorderWidth))
            ? appearance[QLatin1String(JsonKeys::BorderWidth)].toInt()
            : Defaults::BorderWidth;
        zone[JsonKeys::BorderRadius] = appearance.contains(QLatin1String(JsonKeys::BorderRadius))
            ? appearance[QLatin1String(JsonKeys::BorderRadius)].toInt()
            : Defaults::BorderRadius;
        // Use consistent key format - normalize to QString for QVariantMap storage
        // QVariantMap uses QString keys, so convert QLatin1String to QString
        QString useCustomColorsKey = QString::fromLatin1(JsonKeys::UseCustomColors);
        bool useCustomColorsValue = appearance.contains(QLatin1String(JsonKeys::UseCustomColors))
            ? appearance[QLatin1String(JsonKeys::UseCustomColors)].toBool()
            : false;
        zone[useCustomColorsKey] = useCustomColorsValue;

        // Per-zone overlay display mode override (-1 = use layout/global)
        zone[JsonKeys::OverlayDisplayMode] = appearance.contains(QLatin1String(JsonKeys::OverlayDisplayMode))
            ? appearance[QLatin1String(JsonKeys::OverlayDisplayMode)].toInt(-1)
            : -1;

        zones.append(zone);
    }

    if (m_zoneManager) {
        m_zoneManager->setZones(zones);
    }

    // Load shader settings
    m_currentShaderId = layoutObj[QLatin1String(JsonKeys::ShaderId)].toString();
    if (layoutObj.contains(QLatin1String(JsonKeys::ShaderParams))) {
        m_currentShaderParams = layoutObj[QLatin1String(JsonKeys::ShaderParams)].toObject().toVariantMap();
    } else {
        m_currentShaderParams.clear();
    }

    // Load visibility filtering allow-lists
    LayoutUtils::deserializeAllowLists(layoutObj, m_allowedScreens, m_allowedDesktopsInt, m_allowedActivities);

    // Query available context info from daemon via D-Bus
    // Clear first so stale data is not shown if daemon is unavailable
    m_availableScreenIds.clear();
    m_virtualDesktopCount = 1;
    m_virtualDesktopNames.clear();
    m_activitiesAvailable = false;
    m_availableActivities.clear();
    {
        QDBusInterface iface{QString(DBus::ServiceName), QString(DBus::ObjectPath),
                             QString(DBus::Interface::LayoutManager)};
        if (iface.isValid()) {
            // Screen IDs (stable EDID-based identifiers)
            QDBusReply<QString> screensReply = iface.call(QStringLiteral("getAllScreenAssignments"));
            if (screensReply.isValid()) {
                const QJsonObject screensObj = QJsonDocument::fromJson(screensReply.value().toUtf8()).object();
                for (auto it = screensObj.begin(); it != screensObj.end(); ++it) {
                    // Prefer the screenId field if present, fall back to connector name key
                    QString screenId = it.value().toObject().value(QStringLiteral("screenId")).toString();
                    if (screenId.isEmpty()) {
                        screenId = it.key();
                    }
                    m_availableScreenIds.append(screenId);
                }
            }

            // Virtual desktops
            QDBusReply<int> countReply = iface.call(QStringLiteral("getVirtualDesktopCount"));
            if (countReply.isValid()) {
                m_virtualDesktopCount = countReply.value();
            }
            QDBusReply<QStringList> namesReply = iface.call(QStringLiteral("getVirtualDesktopNames"));
            if (namesReply.isValid()) {
                m_virtualDesktopNames = namesReply.value();
            }

            // Activities
            QDBusReply<bool> activitiesReply = iface.call(QStringLiteral("isActivitiesAvailable"));
            if (activitiesReply.isValid()) {
                m_activitiesAvailable = activitiesReply.value();
            }
            if (m_activitiesAvailable) {
                QDBusReply<QString> allActivitiesReply = iface.call(QStringLiteral("getAllActivitiesInfo"));
                if (allActivitiesReply.isValid()) {
                    QJsonDocument activitiesDoc = QJsonDocument::fromJson(allActivitiesReply.value().toUtf8());
                    if (activitiesDoc.isArray()) {
                        const auto arr = activitiesDoc.array();
                        for (const auto& v : arr) {
                            m_availableActivities.append(v.toObject().toVariantMap());
                        }
                    }
                }
            }
        }
    }

    // Load per-layout gap overrides (-1 = use global setting)
    int oldZonePadding = m_zonePadding;
    bool oldUseFullScreen = m_useFullScreenGeometry;
    m_zonePadding = layoutObj.contains(QLatin1String(JsonKeys::ZonePadding))
        ? layoutObj[QLatin1String(JsonKeys::ZonePadding)].toInt(-1)
        : -1;
    m_outerGap = layoutObj.contains(QLatin1String(JsonKeys::OuterGap))
        ? layoutObj[QLatin1String(JsonKeys::OuterGap)].toInt(-1)
        : -1;
    m_usePerSideOuterGap = layoutObj[QLatin1String(JsonKeys::UsePerSideOuterGap)].toBool(false);
    m_outerGapTop = layoutObj.contains(QLatin1String(JsonKeys::OuterGapTop))
        ? layoutObj[QLatin1String(JsonKeys::OuterGapTop)].toInt(-1)
        : -1;
    m_outerGapBottom = layoutObj.contains(QLatin1String(JsonKeys::OuterGapBottom))
        ? layoutObj[QLatin1String(JsonKeys::OuterGapBottom)].toInt(-1)
        : -1;
    m_outerGapLeft = layoutObj.contains(QLatin1String(JsonKeys::OuterGapLeft))
        ? layoutObj[QLatin1String(JsonKeys::OuterGapLeft)].toInt(-1)
        : -1;
    m_outerGapRight = layoutObj.contains(QLatin1String(JsonKeys::OuterGapRight))
        ? layoutObj[QLatin1String(JsonKeys::OuterGapRight)].toInt(-1)
        : -1;
    m_useFullScreenGeometry = layoutObj[QLatin1String(JsonKeys::UseFullScreenGeometry)].toBool(false);
    if (layoutObj.contains(QLatin1String(JsonKeys::AspectRatioClassKey))) {
        QJsonValue arVal = layoutObj[QLatin1String(JsonKeys::AspectRatioClassKey)];
        if (arVal.isString()) {
            // Canonical format from Layout::toJson() — string like "ultrawide"
            m_aspectRatioClass = static_cast<int>(ScreenClassification::fromString(arVal.toString()));
        } else {
            // Int format (from editor save round-trip before daemon persists)
            m_aspectRatioClass = arVal.toInt(0);
        }
    } else {
        m_aspectRatioClass = 0;
    }
    int oldOverlayDisplayMode = m_overlayDisplayMode;
    m_overlayDisplayMode = layoutObj.contains(QLatin1String(JsonKeys::OverlayDisplayMode))
        ? layoutObj[QLatin1String(JsonKeys::OverlayDisplayMode)].toInt(-1)
        : -1;

    m_selectedZoneId.clear();
    m_selectedZoneIds.clear();
    m_isNewLayout = false;
    m_hasUnsavedChanges = false;

    // Clear undo stack when loading a layout
    if (m_undoController) {
        m_undoController->clear();
    }

    // Refresh available shaders from daemon
    refreshAvailableShaders();

    // Update cached shader parameters after refresh (needs D-Bus access)
    if (ShaderRegistry::isNoneShader(m_currentShaderId)) {
        m_cachedShaderParameters.clear();
    } else {
        QVariantMap info = getShaderInfo(m_currentShaderId);
        if (info.contains(QLatin1String("parameters"))) {
            m_cachedShaderParameters = info.value(QLatin1String("parameters")).toList();
        } else {
            m_cachedShaderParameters.clear();
        }
    }

    // Strip stale params accumulated from previous shader selections
    if (!m_currentShaderParams.isEmpty() && !m_cachedShaderParameters.isEmpty()) {
        m_currentShaderParams = stripStaleShaderParams(m_currentShaderParams);
    }

    ++m_zonesVersion;
    Q_EMIT layoutIdChanged();
    Q_EMIT layoutNameChanged();
    Q_EMIT zonesChanged();
    Q_EMIT selectedZoneIdChanged();
    Q_EMIT selectedZoneIdsChanged();
    Q_EMIT hasUnsavedChangesChanged();
    Q_EMIT currentShaderIdChanged();
    Q_EMIT currentShaderParamsChanged();
    Q_EMIT currentShaderParametersChanged();

    // Emit gap change signals
    if (m_zonePadding != oldZonePadding) {
        Q_EMIT zonePaddingChanged();
    }
    // Intentional policy exception to signal-on-change rule:
    // always emit outerGapChanged here because per-side gap values (top/bottom/left/right)
    // can differ between layouts even when m_outerGap is numerically unchanged.
    Q_EMIT outerGapChanged();
    if (m_useFullScreenGeometry != oldUseFullScreen) {
        Q_EMIT useFullScreenGeometryChanged();
    }
    Q_EMIT aspectRatioClassChanged();
    if (m_overlayDisplayMode != oldOverlayDisplayMode) {
        Q_EMIT overlayDisplayModeChanged();
    }

    // Emit visibility filtering signals
    Q_EMIT allowedScreensChanged();
    Q_EMIT allowedDesktopsChanged();
    Q_EMIT allowedActivitiesChanged();
    Q_EMIT availableScreenIdsChanged();
    Q_EMIT virtualDesktopCountChanged();
    Q_EMIT virtualDesktopNamesChanged();
    Q_EMIT activitiesAvailableChanged();
    Q_EMIT availableActivitiesChanged();
}

/**
 * @brief Saves the current layout to the daemon
 *
 * Serializes the layout to JSON and sends it to the daemon via D-Bus.
 * Creates a new layout if isNewLayout is true, otherwise updates existing layout.
 * Emits layoutSaveFailed signal on error, layoutSaved on success.
 */
void EditorController::saveLayout()
{
    if (!m_layoutService || !m_zoneManager) {
        Q_EMIT layoutSaveFailed(PzI18n::tr("Services not initialized"));
        return;
    }

    // Build JSON from current state
    QJsonObject layoutObj;
    layoutObj[QLatin1String(JsonKeys::Id)] = m_layoutId;
    layoutObj[QLatin1String(JsonKeys::Name)] = m_layoutName;
    layoutObj[QLatin1String(JsonKeys::IsBuiltIn)] = false;

    QJsonArray zonesArray;
    QVariantList zones = m_zoneManager->zones();
    for (const QVariant& zoneVar : zones) {
        QVariantMap zone = zoneVar.toMap();
        QJsonObject zoneObj;

        zoneObj[QLatin1String(JsonKeys::Id)] = zone[JsonKeys::Id].toString();
        zoneObj[QLatin1String(JsonKeys::Name)] = zone[JsonKeys::Name].toString();
        zoneObj[QLatin1String(JsonKeys::ZoneNumber)] = zone[JsonKeys::ZoneNumber].toInt();

        QJsonObject relGeo;
        relGeo[QLatin1String(JsonKeys::X)] = zone[JsonKeys::X].toDouble();
        relGeo[QLatin1String(JsonKeys::Y)] = zone[JsonKeys::Y].toDouble();
        relGeo[QLatin1String(JsonKeys::Width)] = zone[JsonKeys::Width].toDouble();
        relGeo[QLatin1String(JsonKeys::Height)] = zone[JsonKeys::Height].toDouble();
        zoneObj[QLatin1String(JsonKeys::RelativeGeometry)] = relGeo;

        // Write geometry mode and fixed geometry when Fixed
        int geoMode = zone.value(JsonKeys::GeometryMode, 0).toInt();
        if (geoMode == static_cast<int>(ZoneGeometryMode::Fixed)) {
            zoneObj[QLatin1String(JsonKeys::GeometryMode)] = geoMode;
            QJsonObject fixedGeo;
            fixedGeo[QLatin1String(JsonKeys::X)] = zone.value(JsonKeys::FixedX, 0.0).toDouble();
            fixedGeo[QLatin1String(JsonKeys::Y)] = zone.value(JsonKeys::FixedY, 0.0).toDouble();
            fixedGeo[QLatin1String(JsonKeys::Width)] = zone.value(JsonKeys::FixedWidth, 0.0).toDouble();
            fixedGeo[QLatin1String(JsonKeys::Height)] = zone.value(JsonKeys::FixedHeight, 0.0).toDouble();
            zoneObj[QLatin1String(JsonKeys::FixedGeometry)] = fixedGeo;
        }

        QJsonObject appearance;
        appearance[QLatin1String(JsonKeys::HighlightColor)] = zone[JsonKeys::HighlightColor].toString();
        appearance[QLatin1String(JsonKeys::InactiveColor)] = zone[JsonKeys::InactiveColor].toString();
        appearance[QLatin1String(JsonKeys::BorderColor)] = zone[JsonKeys::BorderColor].toString();
        // Include all appearance properties for persistence
        appearance[QLatin1String(JsonKeys::ActiveOpacity)] =
            zone.contains(JsonKeys::ActiveOpacity) ? zone[JsonKeys::ActiveOpacity].toDouble() : Defaults::Opacity;
        appearance[QLatin1String(JsonKeys::InactiveOpacity)] = zone.contains(JsonKeys::InactiveOpacity)
            ? zone[JsonKeys::InactiveOpacity].toDouble()
            : Defaults::InactiveOpacity;
        appearance[QLatin1String(JsonKeys::BorderWidth)] =
            zone.contains(JsonKeys::BorderWidth) ? zone[JsonKeys::BorderWidth].toInt() : Defaults::BorderWidth;
        appearance[QLatin1String(JsonKeys::BorderRadius)] =
            zone.contains(JsonKeys::BorderRadius) ? zone[JsonKeys::BorderRadius].toInt() : Defaults::BorderRadius;
        // Use consistent key format - normalize to QString for QVariantMap lookup
        // QVariantMap uses QString keys, so convert QLatin1String to QString
        QString useCustomColorsKey = QString::fromLatin1(JsonKeys::UseCustomColors);
        appearance[QLatin1String(JsonKeys::UseCustomColors)] =
            zone.contains(useCustomColorsKey) ? zone[useCustomColorsKey].toBool() : false;
        // Per-zone overlay display mode override (only if set)
        int zoneOverlayMode = zone.value(JsonKeys::OverlayDisplayMode, -1).toInt();
        if (zoneOverlayMode >= 0) {
            appearance[QLatin1String(JsonKeys::OverlayDisplayMode)] = zoneOverlayMode;
        }
        zoneObj[QLatin1String(JsonKeys::Appearance)] = appearance;

        zonesArray.append(zoneObj);
    }
    layoutObj[QLatin1String(JsonKeys::Zones)] = zonesArray;

    // Include shader settings (strip stale params from other shaders)
    if (!ShaderRegistry::isNoneShader(m_currentShaderId)) {
        layoutObj[QLatin1String(JsonKeys::ShaderId)] = m_currentShaderId;
    }
    if (!m_currentShaderParams.isEmpty()) {
        // Only persist params that belong to the current shader
        QVariantMap cleanParams = stripStaleShaderParams(m_currentShaderParams);
        if (!cleanParams.isEmpty()) {
            layoutObj[QLatin1String(JsonKeys::ShaderParams)] = QJsonObject::fromVariantMap(cleanParams);
        }
    }

    // Include per-layout gap overrides (only if set, >= 0)
    if (m_zonePadding >= 0) {
        layoutObj[QLatin1String(JsonKeys::ZonePadding)] = m_zonePadding;
    }
    if (m_outerGap >= 0) {
        layoutObj[QLatin1String(JsonKeys::OuterGap)] = m_outerGap;
    }
    // Serialize per-side toggle whenever enabled so user intent is preserved across save/load
    if (m_usePerSideOuterGap) {
        layoutObj[QLatin1String(JsonKeys::UsePerSideOuterGap)] = true;
        if (m_outerGapTop >= 0)
            layoutObj[QLatin1String(JsonKeys::OuterGapTop)] = m_outerGapTop;
        if (m_outerGapBottom >= 0)
            layoutObj[QLatin1String(JsonKeys::OuterGapBottom)] = m_outerGapBottom;
        if (m_outerGapLeft >= 0)
            layoutObj[QLatin1String(JsonKeys::OuterGapLeft)] = m_outerGapLeft;
        if (m_outerGapRight >= 0)
            layoutObj[QLatin1String(JsonKeys::OuterGapRight)] = m_outerGapRight;
    }

    // Include per-layout overlay display mode override (only if set)
    if (m_overlayDisplayMode >= 0) {
        layoutObj[QLatin1String(JsonKeys::OverlayDisplayMode)] = m_overlayDisplayMode;
    }

    // Include full screen geometry mode (only if enabled)
    if (m_useFullScreenGeometry) {
        layoutObj[QLatin1String(JsonKeys::UseFullScreenGeometry)] = true;
    }

    // Include aspect ratio class (only if not Any) — serialize as int for updateLayout D-Bus,
    // which converts to the canonical string format via Layout::setAspectRatioClassInt()
    if (m_aspectRatioClass != 0) {
        layoutObj[QLatin1String(JsonKeys::AspectRatioClassKey)] = m_aspectRatioClass;
    }

    // Include visibility filtering allow-lists (only if non-empty)
    LayoutUtils::serializeAllowLists(layoutObj, m_allowedScreens, m_allowedDesktopsInt, m_allowedActivities);

    QJsonDocument doc(layoutObj);
    QString jsonStr = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));

    // Use layout service to save
    if (m_isNewLayout) {
        QString newLayoutId = m_layoutService->createLayout(jsonStr);
        if (newLayoutId.isEmpty()) {
            // Error signal already emitted by service
            return;
        }
        m_layoutId = newLayoutId;
        m_isNewLayout = false;
    } else {
        bool success = m_layoutService->updateLayout(jsonStr);
        if (!success) {
            // Error signal already emitted by service
            return;
        }
    }

    m_hasUnsavedChanges = false;

    // Mark undo stack as clean after successful save
    if (m_undoController) {
        m_undoController->setClean();
    }

    // Note: We intentionally do NOT assign the layout to a screen here.
    // Layout assignment should be a separate, explicit user action.
    // This prevents saving a layout from inadvertently changing the active layout.

    Q_EMIT hasUnsavedChangesChanged();
    Q_EMIT layoutSaved();
}

/**
 * @brief Discards unsaved changes and closes the editor
 *
 * Reloads the layout from the daemon if it's not a new layout,
 * effectively discarding any unsaved changes.
 */
void EditorController::discardChanges()
{
    if (!m_isNewLayout && !m_layoutId.isEmpty()) {
        loadLayout(m_layoutId);
    }
    Q_EMIT editorClosed();
}

// ---------------------------------------------------------------------------
// Group 3 - Import/Export
// ---------------------------------------------------------------------------

void EditorController::importLayout(const QString& filePath)
{
    if (filePath.isEmpty()) {
        Q_EMIT layoutLoadFailed(QCoreApplication::translate("EditorController", "File path cannot be empty"));
        return;
    }

    QDBusInterface layoutManager(QString::fromLatin1(DBus::ServiceName), QString::fromLatin1(DBus::ObjectPath),
                                 QString::fromLatin1(DBus::Interface::LayoutManager), QDBusConnection::sessionBus());

    if (!layoutManager.isValid()) {
        QString error = QCoreApplication::translate("EditorController", "Cannot connect to PlasmaZones daemon");
        qCWarning(lcEditor) << error;
        Q_EMIT layoutLoadFailed(error);
        return;
    }

    QDBusReply<QString> reply = layoutManager.call(QStringLiteral("importLayout"), filePath);
    if (!reply.isValid()) {
        QString error =
            QCoreApplication::translate("EditorController", "Failed to import layout: %1").arg(reply.error().message());
        qCWarning(lcEditor) << error;
        Q_EMIT layoutLoadFailed(error);
        return;
    }

    QString newLayoutId = reply.value();
    if (newLayoutId.isEmpty()) {
        QString error = QCoreApplication::translate("EditorController", "Imported layout but received empty ID");
        qCWarning(lcEditor) << error;
        Q_EMIT layoutLoadFailed(error);
        return;
    }

    // Load the imported layout into the editor
    loadLayout(newLayoutId);
}

/**
 * @brief Exports the current layout to a JSON file
 * @param filePath Path where the JSON file should be saved
 *
 * Calls the D-Bus exportLayout method to save the current layout to a file.
 * Emits layoutSaveFailed if the export fails.
 */
void EditorController::exportLayout(const QString& filePath)
{
    if (filePath.isEmpty()) {
        Q_EMIT layoutSaveFailed(QCoreApplication::translate("EditorController", "File path cannot be empty"));
        return;
    }

    if (m_layoutId.isEmpty()) {
        Q_EMIT layoutSaveFailed(QCoreApplication::translate("EditorController", "No layout loaded to export"));
        return;
    }

    QDBusInterface layoutManager(QString::fromLatin1(DBus::ServiceName), QString::fromLatin1(DBus::ObjectPath),
                                 QString::fromLatin1(DBus::Interface::LayoutManager), QDBusConnection::sessionBus());

    if (!layoutManager.isValid()) {
        QString error = QCoreApplication::translate("EditorController", "Cannot connect to PlasmaZones daemon");
        qCWarning(lcEditor) << error;
        Q_EMIT layoutSaveFailed(error);
        return;
    }

    QDBusReply<void> reply = layoutManager.call(QStringLiteral("exportLayout"), m_layoutId, filePath);
    if (!reply.isValid()) {
        QString error =
            QCoreApplication::translate("EditorController", "Failed to export layout: %1").arg(reply.error().message());
        qCWarning(lcEditor) << error;
        Q_EMIT layoutSaveFailed(error);
        return;
    }

    // Export successful - emit layoutSaved signal for success notification
    Q_EMIT layoutSaved();
}

} // namespace PlasmaZones
