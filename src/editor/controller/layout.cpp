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
#include "../../core/wayland_raise.h"
#include "../../../shared/virtualscreenid.h"

#include "pz_i18n.h"
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
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

void EditorController::cacheVirtualScreenGeometry(const QString& screenName)
{
    m_virtualScreenSize = QSize();
    m_virtualScreenRect = QRect();
    if (!VirtualScreenId::isVirtual(screenName)) {
        return;
    }
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QString::fromLatin1(DBus::ServiceName), QString::fromLatin1(DBus::ObjectPath),
        QString::fromLatin1(DBus::Interface::Screen), QStringLiteral("getScreenGeometry"));
    msg << screenName;
    QDBusMessage reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, 2000);
    if (reply.type() == QDBusMessage::ReplyMessage && reply.arguments().size() >= 1) {
        QRect geo = qdbus_cast<QRect>(reply.arguments().at(0));
        if (geo.isValid()) {
            m_virtualScreenSize = geo.size();
            QString physId = VirtualScreenId::extractPhysicalId(screenName);
            QScreen* physScreen = Utils::findScreenByIdOrName(physId);
            QPoint physOrigin = physScreen ? physScreen->geometry().topLeft() : QPoint();
            m_virtualScreenRect = QRect(geo.topLeft() - physOrigin, geo.size());
            qCDebug(lcEditor) << "Virtual screen" << screenName << "geometry:" << geo
                              << "relative rect:" << m_virtualScreenRect;
        }
    }
}

QVariantList EditorController::screenModel() const
{
    QVariantList model;

    if (m_availableScreenIds.isEmpty()) {
        // Fallback: use Qt's physical screens (editor opened before daemon responded)
        for (QScreen* screen : QGuiApplication::screens()) {
            QVariantMap entry;
            entry[QStringLiteral("name")] = Utils::screenIdentifier(screen);
            entry[QStringLiteral("displayName")] = screen->name();
            model.append(entry);
        }
        return model;
    }

    // Cache VS display names per physical screen to avoid repeated D-Bus calls
    QHash<QString, QJsonArray> vsConfigCache;

    // Build from daemon's effective screen IDs (includes virtual screens)
    for (const QString& screenId : m_availableScreenIds) {
        QVariantMap entry;
        entry[QStringLiteral("name")] = screenId;
        if (VirtualScreenId::isVirtual(screenId)) {
            // Use user-configured display name from VS config, fall back to generic label
            QString vsDisplayName;
            QString physId = VirtualScreenId::extractPhysicalId(screenId);
            int vsIndex = VirtualScreenId::extractIndex(screenId);
            if (vsIndex >= 0) {
                if (!vsConfigCache.contains(physId)) {
                    QDBusMessage msg = QDBusMessage::createMethodCall(
                        QString::fromLatin1(DBus::ServiceName), QString::fromLatin1(DBus::ObjectPath),
                        QString::fromLatin1(DBus::Interface::Screen), QStringLiteral("getVirtualScreenConfig"));
                    msg << physId;
                    QDBusMessage reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, 2000);
                    if (reply.type() == QDBusMessage::ReplyMessage && reply.arguments().size() >= 1) {
                        QJsonObject root =
                            QJsonDocument::fromJson(reply.arguments().at(0).toString().toUtf8()).object();
                        vsConfigCache[physId] = root.value(QStringLiteral("screens")).toArray();
                    } else {
                        vsConfigCache[physId] = QJsonArray();
                    }
                }
                const QJsonArray& screens = vsConfigCache[physId];
                if (vsIndex < screens.size()) {
                    vsDisplayName = screens[vsIndex].toObject().value(QStringLiteral("displayName")).toString();
                }
            }
            if (vsDisplayName.isEmpty()) {
                vsDisplayName = QStringLiteral("VS%1").arg(vsIndex + 1);
            }
            entry[QStringLiteral("displayName")] = vsDisplayName;
        } else {
            // Physical screen: use connector name for brevity
            QScreen* screen = Utils::findScreenByIdOrName(screenId);
            entry[QStringLiteral("displayName")] = screen ? screen->name() : screenId;
        }
        model.append(entry);
    }

    return model;
}

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

        cacheVirtualScreenGeometry(screenName);

        Q_EMIT targetScreenChanged();
        Q_EMIT targetScreenSizeChanged();
        refreshUsableAreaInsets();
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

namespace {

/// Plan for where/how to show the editor window. Computed from the target
/// screen + virtual screen rect, then applied via a single code path whether
/// the window is visible or not, virtual-screen or physical.
struct EditorWindowPlan
{
    QScreen* screen = nullptr; ///< Physical QScreen to map onto.
    QRect geometry; ///< Absolute geometry to set before showing.
    bool fullScreen = false; ///< true → showFullScreen(); false → show() (used for VS region).
    bool frameless = false; ///< true → Qt::FramelessWindowHint (used for VS region).

    bool isValid() const
    {
        return screen != nullptr;
    }
};

/// Apply the computed plan to a window and present it. Used as the "apply
/// geometry then show" lambda inside both the deferred (visible → destroy-and-
/// remap) and direct (hidden → apply immediately) paths.
void applyEditorWindowPlan(QQuickWindow* win, const EditorWindowPlan& plan)
{
    win->setFlag(Qt::FramelessWindowHint, plan.frameless);
    win->setScreen(plan.screen);
    win->setGeometry(plan.geometry);
    if (plan.fullScreen) {
        win->showFullScreen();
    } else {
        win->show();
    }
    // Focus-steal: KWin grants focus to freshly-mapped xdg_toplevels within
    // the same session by default. raise()/requestActivate() nudge the
    // compositor in case it doesn't.
    win->raise();
    win->requestActivate();
}

} // anonymous namespace

void EditorController::showFullScreenOnTargetScreen(QQuickWindow* window)
{
    if (!window) {
        return;
    }

    // Cache the primary editor window the first time QML hands it to us. The
    // D-Bus single-instance slots (raise / handleLaunchRequest) need a reliable
    // pointer to this window and can't rely on QGuiApplication::allWindows()
    // ordering, which may surface shader-preview or dialog windows first.
    if (!m_primaryWindow) {
        m_primaryWindow = window;
    }

    // No target screen → plain fullscreen on whatever output Qt picks.
    if (m_targetScreen.isEmpty()) {
        window->showFullScreen();
        window->raise();
        window->requestActivate();
        return;
    }

    // Build a single plan covering both the virtual-screen (frameless, sized
    // to VS region) and physical-screen (full monitor) cases.
    EditorWindowPlan plan;
    if (m_virtualScreenRect.isValid()) {
        const QString physId = VirtualScreenId::extractPhysicalId(m_targetScreen);
        if (QScreen* physScreen = Utils::findScreenByIdOrName(physId)) {
            plan.screen = physScreen;
            // Absolute VS coordinates = physical screen origin + VS offset.
            plan.geometry =
                QRect(physScreen->geometry().topLeft() + m_virtualScreenRect.topLeft(), m_virtualScreenRect.size());
            plan.fullScreen = false;
            plan.frameless = true;
        }
    }
    if (!plan.isValid()) {
        // Physical-screen path: match by identifier, take full geometry.
        for (QScreen* screen : QGuiApplication::screens()) {
            if (Utils::screenIdentifier(screen) == m_targetScreen || screen->name() == m_targetScreen) {
                plan.screen = screen;
                plan.geometry = screen->geometry();
                plan.fullScreen = true;
                plan.frameless = false;
                break;
            }
        }
    }
    if (!plan.isValid()) {
        // Unknown target — fall back to plain fullscreen and force focus.
        window->setFlag(Qt::FramelessWindowHint, false);
        window->showFullScreen();
        window->raise();
        window->requestActivate();
        return;
    }

    qCDebug(lcEditor) << "Editor window plan — screen:" << plan.screen->name() << "geometry:" << plan.geometry
                      << "fullScreen:" << plan.fullScreen << "frameless:" << plan.frameless;

    // Same-screen-and-already-visible fast path: no layout change needed, so
    // just force the compositor to bring the window to the front. On Wayland
    // a plain raise() on an already-mapped xdg_toplevel is a no-op without an
    // activation token — forceBringToFront() does the destroy-and-remap dance
    // KWin treats as a new window presentation.
    if (window->screen() == plan.screen && window->isVisible() && window->isExposed()) {
        forceBringToFront(window);
        return;
    }

    // Visible but needs relayout (screen switch or VS/physical toggle): Wayland
    // binds wl_output at surface-creation time, so we must destroy the native
    // window and let Qt recreate it with the new output. Defer to the next
    // event-loop tick so we don't tear down the platform surface from inside
    // a paint or D-Bus dispatch.
    if (window->isVisible()) {
        QPointer<QQuickWindow> safeWindow(window);
        QTimer::singleShot(0, window, [safeWindow, plan]() {
            if (!safeWindow || !plan.screen) {
                return;
            }
            safeWindow->destroy();
            applyEditorWindowPlan(safeWindow.data(), plan);
        });
        return;
    }

    // Not yet visible — apply directly, no remap needed.
    applyEditorWindowPlan(window, plan);
}

void EditorController::setTargetScreenDirect(const QString& screenName)
{
    // Sets the target screen without loading a layout - used during initialization
    // when a layout is explicitly specified via command line
    if (m_targetScreen != screenName) {
        m_targetScreen = screenName;

        cacheVirtualScreenGeometry(screenName);

        Q_EMIT targetScreenChanged();
        Q_EMIT targetScreenSizeChanged();
        refreshUsableAreaInsets();
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
        Q_EMIT layoutLoadFailed(PzI18n::tr("File path cannot be empty"));
        return;
    }

    QDBusInterface layoutManager(QString::fromLatin1(DBus::ServiceName), QString::fromLatin1(DBus::ObjectPath),
                                 QString::fromLatin1(DBus::Interface::LayoutManager), QDBusConnection::sessionBus());

    if (!layoutManager.isValid()) {
        QString error = PzI18n::tr("Cannot connect to PlasmaZones daemon");
        qCWarning(lcEditor) << error;
        Q_EMIT layoutLoadFailed(error);
        return;
    }

    QDBusReply<QString> reply = layoutManager.call(QStringLiteral("importLayout"), filePath);
    if (!reply.isValid()) {
        QString error = PzI18n::tr("Failed to import layout: %1").arg(reply.error().message());
        qCWarning(lcEditor) << error;
        Q_EMIT layoutLoadFailed(error);
        return;
    }

    QString newLayoutId = reply.value();
    if (newLayoutId.isEmpty()) {
        QString error = PzI18n::tr("Imported layout but received empty ID");
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
        Q_EMIT layoutSaveFailed(PzI18n::tr("File path cannot be empty"));
        return;
    }

    if (m_layoutId.isEmpty()) {
        Q_EMIT layoutSaveFailed(PzI18n::tr("No layout loaded to export"));
        return;
    }

    QDBusInterface layoutManager(QString::fromLatin1(DBus::ServiceName), QString::fromLatin1(DBus::ObjectPath),
                                 QString::fromLatin1(DBus::Interface::LayoutManager), QDBusConnection::sessionBus());

    if (!layoutManager.isValid()) {
        QString error = PzI18n::tr("Cannot connect to PlasmaZones daemon");
        qCWarning(lcEditor) << error;
        Q_EMIT layoutSaveFailed(error);
        return;
    }

    QDBusReply<void> reply = layoutManager.call(QStringLiteral("exportLayout"), m_layoutId, filePath);
    if (!reply.isValid()) {
        QString error = PzI18n::tr("Failed to export layout: %1").arg(reply.error().message());
        qCWarning(lcEditor) << error;
        Q_EMIT layoutSaveFailed(error);
        return;
    }

    // Export successful - emit layoutSaved signal for success notification
    Q_EMIT layoutSaved();
}

} // namespace PlasmaZones
