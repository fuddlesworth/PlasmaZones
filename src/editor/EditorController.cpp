// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "EditorController.h"
#include "services/ILayoutService.h"
#include "services/DBusLayoutService.h"
#include "services/ZoneManager.h"
#include "services/SnappingService.h"
#include "services/TemplateService.h"
#include "undo/UndoController.h"
#include "undo/commands/AddZoneCommand.h"
#include "undo/commands/DeleteZoneCommand.h"
#include "undo/commands/UpdateZoneGeometryCommand.h"
#include "undo/commands/UpdateZoneNameCommand.h"
#include "undo/commands/UpdateZoneNumberCommand.h"
#include "undo/commands/UpdateZoneAppearanceCommand.h"
#include "undo/commands/DuplicateZoneCommand.h"
#include "undo/commands/SplitZoneCommand.h"
#include "undo/commands/FillZoneCommand.h"
#include "undo/commands/ChangeZOrderCommand.h"
#include "undo/commands/ApplyTemplateCommand.h"
#include "undo/commands/ClearAllZonesCommand.h"
#include "undo/commands/UpdateLayoutNameCommand.h"
#include "undo/commands/DeleteZoneWithFillCommand.h"
#include "undo/commands/ChangeSelectionCommand.h"
#include "undo/commands/BatchUpdateAppearanceCommand.h"
#include "undo/commands/PasteZonesCommand.h"
#include "undo/commands/DividerResizeCommand.h"
#include "undo/commands/UpdateShaderIdCommand.h"
#include "undo/commands/UpdateShaderParamsCommand.h"
#include "undo/commands/UpdateGapOverrideCommand.h"
#include "undo/commands/UpdateVisibilityCommand.h"
#include "../core/constants.h"
#include "../core/layoututils.h"
#include "../core/logging.h"
#include "../core/shaderregistry.h"
#include "../core/dbusvariantutils.h"
#include "helpers/ZoneSerialization.h"
#include "helpers/SettingsDbusQueries.h"
#include "helpers/ShaderDbusQueries.h"
#include "helpers/BatchOperationScope.h"

#include <KConfig>
#include <KConfigGroup>
#include <KSharedConfig>
#include <KLocalizedString>
#include <QCoreApplication>
#include <QClipboard>
#include <QColor>
#include <QMimeData>
#include <QGuiApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusVariant>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QPointer>
#include <QTimer>
#include <QUuid>
#include <QtMath>
#include <QRegularExpression>
#include <utility>

namespace PlasmaZones {

EditorController::EditorController(QObject* parent)
    : QObject(parent)
    , m_layoutService(new DBusLayoutService(this))
    , m_zoneManager(new ZoneManager(this))
    , m_snappingService(new SnappingService(this))
    , m_templateService(new TemplateService(this))
    , m_undoController(new UndoController(this))
{
    // Connect service signals
    connect(m_layoutService, &ILayoutService::errorOccurred, this, [this](const QString& error) {
        Q_EMIT layoutLoadFailed(error);
        Q_EMIT layoutSaveFailed(error);
    });

    connect(m_zoneManager, &ZoneManager::zonesChanged, this, [this]() {
        // Check if selected zones still exist after zones changed
        // This handles cases where restoreZones() or clearAllZones() removes selected zones
        if (!m_selectedZoneIds.isEmpty() && m_zoneManager) {
            QStringList validZoneIds;
            for (const QString& zoneId : m_selectedZoneIds) {
                QVariantMap zone = m_zoneManager->getZoneById(zoneId);
                if (!zone.isEmpty()) {
                    validZoneIds.append(zoneId);
                }
            }
            if (validZoneIds != m_selectedZoneIds) {
                m_selectedZoneIds = validZoneIds;
                QString newSelectedId = validZoneIds.isEmpty() ? QString() : validZoneIds.first();
                if (m_selectedZoneId != newSelectedId) {
                    m_selectedZoneId = newSelectedId;
                    Q_EMIT selectedZoneIdChanged();
                }
                Q_EMIT selectedZoneIdsChanged();
            }
        }
        ++m_zonesVersion;
        Q_EMIT zonesChanged();
    });
    connect(m_zoneManager, &ZoneManager::zoneAdded, this, &EditorController::zoneAdded);
    connect(m_zoneManager, &ZoneManager::zoneRemoved, this, [this](const QString& zoneId) {
        // Remove zone from selection if it was selected
        if (m_selectedZoneIds.contains(zoneId)) {
            m_selectedZoneIds.removeAll(zoneId);
            syncSelectionSignals();
        }
        Q_EMIT zoneRemoved(zoneId);
    });
    connect(m_zoneManager, &ZoneManager::zoneGeometryChanged, this, &EditorController::zoneGeometryChanged);
    connect(m_zoneManager, &ZoneManager::zoneNameChanged, this, &EditorController::zoneNameChanged);
    connect(m_zoneManager, &ZoneManager::zoneNumberChanged, this, &EditorController::zoneNumberChanged);
    connect(m_zoneManager, &ZoneManager::zoneColorChanged, this, &EditorController::zoneColorChanged);
    connect(m_zoneManager, &ZoneManager::zonesModified, this, &EditorController::markUnsaved);

    connect(m_snappingService, &SnappingService::gridSnappingEnabledChanged, this,
            &EditorController::gridSnappingEnabledChanged);
    connect(m_snappingService, &SnappingService::edgeSnappingEnabledChanged, this,
            &EditorController::edgeSnappingEnabledChanged);
    connect(m_snappingService, &SnappingService::snapIntervalXChanged, this, &EditorController::snapIntervalXChanged);
    connect(m_snappingService, &SnappingService::snapIntervalYChanged, this, &EditorController::snapIntervalYChanged);
    connect(m_snappingService, &SnappingService::snapIntervalChanged, this,
            &EditorController::snapIntervalChanged); // For backward compatibility

    // Connect to clipboard changes for reactive canPaste updates
    QClipboard* clipboard = QGuiApplication::clipboard();
    connect(clipboard, &QClipboard::dataChanged, this, &EditorController::onClipboardChanged);

    // Initialize canPaste state
    m_canPaste = canPaste();

    // Load editor settings from KConfig
    loadEditorSettings();
}

EditorController::~EditorController()
{
    // Save editor settings to KConfig
    saveEditorSettings();

    // Services are QObjects with this as parent, so they'll be deleted automatically
}

// Property getters
QString EditorController::layoutId() const
{
    return m_layoutId;
}
QString EditorController::layoutName() const
{
    return m_layoutName;
}
QVariantList EditorController::zones() const
{
    return m_zoneManager ? m_zoneManager->zones() : QVariantList();
}

QVariantList EditorController::zonesForShaderPreview(int width, int height) const
{
    QVariantList result;
    if (width <= 0 || height <= 0) {
        return result;
    }

    // Use a simplified 2-zone layout (1 and 2 side-by-side) for the shader preview.
    // This matches how the overlay looks with typical layouts and avoids cramming
    // many zone numbers into the small preview area.
    const qreal resW = static_cast<qreal>(width);
    const qreal resH = static_cast<qreal>(height);
    const int padded = 4;
    const qreal hw = (resW - padded * 3) / 2.0; // Two equal halves with gap
    const qreal hh = resH - padded * 2;

    // Stable IDs for demo layout so shader doesn't see churn on every update
    const QString zone1Id = QStringLiteral("{00000000-0000-0000-0000-000000000001}");
    const QString zone2Id = QStringLiteral("{00000000-0000-0000-0000-000000000002}");

    auto makeZone = [&](int zoneNumber, qreal x, qreal y, qreal w, qreal h) {
        QVariantMap out;
        out[QLatin1String(JsonKeys::Id)] = (zoneNumber == 1) ? zone1Id : zone2Id;
        out[QLatin1String(JsonKeys::X)] = x;
        out[QLatin1String(JsonKeys::Y)] = y;
        out[QLatin1String(JsonKeys::Width)] = w;
        out[QLatin1String(JsonKeys::Height)] = h;
        out[QLatin1String(JsonKeys::ZoneNumber)] = zoneNumber;
        out[QLatin1String(JsonKeys::IsHighlighted)] = false;
        const QColor fillColor = Defaults::HighlightColor;
        const qreal alpha = Defaults::Opacity;
        out[QLatin1String("fillR")] = fillColor.redF() * alpha;
        out[QLatin1String("fillG")] = fillColor.greenF() * alpha;
        out[QLatin1String("fillB")] = fillColor.blueF() * alpha;
        out[QLatin1String("fillA")] = alpha;
        const QColor borderColor = Defaults::BorderColor;
        out[QLatin1String("borderR")] = borderColor.redF();
        out[QLatin1String("borderG")] = borderColor.greenF();
        out[QLatin1String("borderB")] = borderColor.blueF();
        out[QLatin1String("borderA")] = borderColor.alphaF();
        out[QLatin1String("shaderBorderRadius")] = 8.0;
        out[QLatin1String("shaderBorderWidth")] = 2.0;
        return out;
    };

    result.append(makeZone(1, padded, padded, hw, hh));
    result.append(makeZone(2, padded * 2 + hw, padded, hw, hh));

    return result;
}

QVariantMap EditorController::translateShaderParams(const QString& shaderId, const QVariantMap& params) const
{
    return ShaderDbusQueries::queryTranslateShaderParams(shaderId, params);
}

void EditorController::showShaderPreviewOverlay(int x, int y, int width, int height, const QString& screenName,
                                                const QString& shaderId, const QString& shaderParamsJson,
                                                const QString& zonesJson)
{
    QDBusInterface iface(QString::fromLatin1(DBus::ServiceName), QString::fromLatin1(DBus::ObjectPath),
                        QString::fromLatin1(DBus::Interface::Overlay), QDBusConnection::sessionBus());
    if (iface.isValid()) {
        iface.asyncCall(QStringLiteral("showShaderPreview"), x, y, width, height, screenName, shaderId,
                        shaderParamsJson, zonesJson);
    }
}

void EditorController::updateShaderPreviewOverlay(int x, int y, int width, int height,
                                                  const QString& shaderParamsJson, const QString& zonesJson)
{
    QDBusInterface iface(QString::fromLatin1(DBus::ServiceName), QString::fromLatin1(DBus::ObjectPath),
                        QString::fromLatin1(DBus::Interface::Overlay), QDBusConnection::sessionBus());
    if (iface.isValid()) {
        iface.asyncCall(QStringLiteral("updateShaderPreview"), x, y, width, height, shaderParamsJson, zonesJson);
    }
}

void EditorController::hideShaderPreviewOverlay()
{
    QDBusInterface iface(QString::fromLatin1(DBus::ServiceName), QString::fromLatin1(DBus::ObjectPath),
                        QString::fromLatin1(DBus::Interface::Overlay), QDBusConnection::sessionBus());
    if (iface.isValid()) {
        // Use synchronous call so hide completes before any in-flight async show can leave
        // a visible overlay after the dialog has closed.
        iface.call(QStringLiteral("hideShaderPreview"));
    }
}

QString EditorController::selectedZoneId() const
{
    return m_selectedZoneId;
}
QStringList EditorController::selectedZoneIds() const
{
    return m_selectedZoneIds;
}
int EditorController::selectionCount() const
{
    return m_selectedZoneIds.count();
}
bool EditorController::hasMultipleSelection() const
{
    return m_selectedZoneIds.count() > 1;
}
bool EditorController::hasUnsavedChanges() const
{
    return m_hasUnsavedChanges;
}
bool EditorController::isNewLayout() const
{
    return m_isNewLayout;
}
bool EditorController::gridSnappingEnabled() const
{
    return m_snappingService->gridSnappingEnabled();
}
bool EditorController::edgeSnappingEnabled() const
{
    return m_snappingService->edgeSnappingEnabled();
}
qreal EditorController::snapIntervalX() const
{
    return m_snappingService->snapIntervalX();
}
qreal EditorController::snapIntervalY() const
{
    return m_snappingService->snapIntervalY();
}
qreal EditorController::snapInterval() const
{
    return snapIntervalX();
} // Backward compatibility
bool EditorController::gridOverlayVisible() const
{
    return m_gridOverlayVisible;
}
QString EditorController::editorDuplicateShortcut() const
{
    return m_editorDuplicateShortcut;
}
QString EditorController::editorSplitHorizontalShortcut() const
{
    return m_editorSplitHorizontalShortcut;
}
QString EditorController::editorSplitVerticalShortcut() const
{
    return m_editorSplitVerticalShortcut;
}
QString EditorController::editorFillShortcut() const
{
    return m_editorFillShortcut;
}
int EditorController::snapOverrideModifier() const
{
    return m_snapOverrideModifier;
}
bool EditorController::fillOnDropEnabled() const
{
    return m_fillOnDropEnabled;
}
int EditorController::fillOnDropModifier() const
{
    return m_fillOnDropModifier;
}
QString EditorController::targetScreen() const
{
    return m_targetScreen;
}
int EditorController::zonePadding() const
{
    return m_zonePadding;
}

int EditorController::outerGap() const
{
    return m_outerGap;
}

bool EditorController::hasZonePaddingOverride() const
{
    return m_zonePadding >= 0;
}

bool EditorController::hasOuterGapOverride() const
{
    return m_outerGap >= 0;
}

int EditorController::globalZonePadding() const
{
    return m_cachedGlobalZonePadding;
}

int EditorController::globalOuterGap() const
{
    return m_cachedGlobalOuterGap;
}

void EditorController::setZonePadding(int padding)
{
    if (padding < -1) {
        padding = -1;
    }
    if (m_zonePadding != padding) {
        auto* cmd = new UpdateGapOverrideCommand(this, UpdateGapOverrideCommand::GapType::ZonePadding,
                                                 m_zonePadding, padding);
        m_undoController->push(cmd);
    }
}

void EditorController::setZonePaddingDirect(int padding)
{
    if (padding < -1) {
        padding = -1;
    }
    if (m_zonePadding != padding) {
        m_zonePadding = padding;
        markUnsaved();
        Q_EMIT zonePaddingChanged();
    }
}

void EditorController::setOuterGap(int gap)
{
    if (gap < -1) {
        gap = -1;
    }
    if (m_outerGap != gap) {
        auto* cmd = new UpdateGapOverrideCommand(this, UpdateGapOverrideCommand::GapType::OuterGap,
                                                 m_outerGap, gap);
        m_undoController->push(cmd);
    }
}

void EditorController::setOuterGapDirect(int gap)
{
    if (gap < -1) {
        gap = -1;
    }
    if (m_outerGap != gap) {
        m_outerGap = gap;
        markUnsaved();
        Q_EMIT outerGapChanged();
    }
}

void EditorController::clearZonePaddingOverride()
{
    setZonePadding(-1);
}

void EditorController::clearOuterGapOverride()
{
    setOuterGap(-1);
}

void EditorController::refreshGlobalZonePadding()
{
    int newValue = SettingsDbusQueries::queryGlobalZonePadding();

    if (m_cachedGlobalZonePadding != newValue) {
        m_cachedGlobalZonePadding = newValue;
        Q_EMIT globalZonePaddingChanged();
    }
}

void EditorController::refreshGlobalOuterGap()
{
    int newValue = SettingsDbusQueries::queryGlobalOuterGap();

    if (m_cachedGlobalOuterGap != newValue) {
        m_cachedGlobalOuterGap = newValue;
        Q_EMIT globalOuterGapChanged();
    }
}

UndoController* EditorController::undoController() const
{
    return m_undoController;
}
bool EditorController::canPaste() const
{
    QClipboard* clipboard = QGuiApplication::clipboard();
    return ZoneSerialization::isValidClipboardFormat(clipboard->text());
}

// Property setters
void EditorController::setLayoutName(const QString& name)
{
    if (m_layoutName != name) {
        if (!m_undoController) {
            qCWarning(lcEditor) << "Cannot update layout name - undo controller is null";
            return;
        }

        QString oldName = m_layoutName;

        // Create and push command
        auto* command = new UpdateLayoutNameCommand(QPointer<EditorController>(this), oldName, name, QString());
        m_undoController->push(command);
        markUnsaved();
    }
}

void EditorController::setLayoutNameDirect(const QString& name)
{
    if (m_layoutName != name) {
        m_layoutName = name;
        Q_EMIT layoutNameChanged();
    }
}

void EditorController::setSelectedZoneId(const QString& zoneId)
{
    if (m_selectedZoneId != zoneId) {
        m_selectedZoneId = zoneId;
        // Sync with multi-selection: single selection = list with one item
        m_selectedZoneIds.clear();
        if (!zoneId.isEmpty()) {
            m_selectedZoneIds.append(zoneId);
        }
        Q_EMIT selectedZoneIdChanged();
        Q_EMIT selectedZoneIdsChanged();
    }
}

void EditorController::setSelectedZoneIds(const QStringList& zoneIds)
{
    if (m_selectedZoneIds != zoneIds) {
        QStringList oldSelection = m_selectedZoneIds;

        // Apply the change
        setSelectedZoneIdsDirect(zoneIds);

        // Push undo command (if undo controller available)
        if (m_undoController) {
            auto* command = new ChangeSelectionCommand(QPointer<EditorController>(this), oldSelection, zoneIds);
            m_undoController->push(command);
        }
    }
}

void EditorController::setSelectedZoneIdsDirect(const QStringList& zoneIds)
{
    if (m_selectedZoneIds != zoneIds) {
        m_selectedZoneIds = zoneIds;
        // Sync with single-selection for backward compatibility
        QString newSelectedId = zoneIds.isEmpty() ? QString() : zoneIds.first();
        if (m_selectedZoneId != newSelectedId) {
            m_selectedZoneId = newSelectedId;
            Q_EMIT selectedZoneIdChanged();
        }
        Q_EMIT selectedZoneIdsChanged();
    }
}

void EditorController::setGridSnappingEnabled(bool enabled)
{
    m_snappingService->setGridSnappingEnabled(enabled);
    saveEditorSettings();
}

void EditorController::setGridOverlayVisible(bool visible)
{
    if (m_gridOverlayVisible != visible) {
        m_gridOverlayVisible = visible;
        Q_EMIT gridOverlayVisibleChanged();
    }
}

void EditorController::setEdgeSnappingEnabled(bool enabled)
{
    m_snappingService->setEdgeSnappingEnabled(enabled);
    saveEditorSettings();
}

void EditorController::setSnapIntervalX(qreal interval)
{
    m_snappingService->setSnapIntervalX(interval);
    saveEditorSettings();
}

void EditorController::setSnapIntervalY(qreal interval)
{
    m_snappingService->setSnapIntervalY(interval);
    saveEditorSettings();
}

void EditorController::setSnapInterval(qreal interval)
{
    // Backward compatibility: set both X and Y to the same value
    setSnapIntervalX(interval);
    setSnapIntervalY(interval);
}

void EditorController::setSnapOverrideModifier(int modifier)
{
    if (m_snapOverrideModifier != modifier) {
        m_snapOverrideModifier = modifier;
        Q_EMIT snapOverrideModifierChanged();
        saveEditorSettings();
    }
}

void EditorController::setFillOnDropEnabled(bool enabled)
{
    if (m_fillOnDropEnabled != enabled) {
        m_fillOnDropEnabled = enabled;
        Q_EMIT fillOnDropEnabledChanged();
        saveEditorSettings();
    }
}

void EditorController::setFillOnDropModifier(int modifier)
{
    if (m_fillOnDropModifier != modifier) {
        m_fillOnDropModifier = modifier;
        Q_EMIT fillOnDropModifierChanged();
        saveEditorSettings();
    }
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
        Q_EMIT targetScreenChanged();

        // Load the layout assigned to this screen
        if (!screenName.isEmpty() && m_layoutService) {
            QString layoutId = m_layoutService->getLayoutIdForScreen(screenName);
            qCDebug(lcEditor) << "setTargetScreen:" << screenName
                              << "daemon returned layoutId:" << layoutId
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
            if (screen->name() == m_targetScreen) {
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
    }
}

/**
 * @brief Creates a new empty layout
 *
 * Generates a new layout ID and initializes an empty layout.
 * Emits signals to notify QML of the new layout state.
 */
void EditorController::createNewLayout()
{
    m_layoutId = QUuid::createUuid().toString();
    m_layoutName = i18n("New Layout");
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

    // Refresh available shaders from daemon
    refreshAvailableShaders();

    ++m_zonesVersion;
    Q_EMIT layoutIdChanged();
    Q_EMIT layoutNameChanged();
    Q_EMIT zonesChanged();
    Q_EMIT selectedZoneIdChanged();
    Q_EMIT selectedZoneIdsChanged();
    Q_EMIT isNewLayoutChanged();
    Q_EMIT hasUnsavedChangesChanged();
    Q_EMIT currentShaderIdChanged();
    Q_EMIT currentShaderParamsChanged();
    Q_EMIT currentShaderParametersChanged();
    Q_EMIT zonePaddingChanged();
    Q_EMIT outerGapChanged();
}

void EditorController::loadLayout(const QString& layoutId)
{
    if (layoutId.isEmpty()) {
        Q_EMIT layoutLoadFailed(i18n("Layout ID cannot be empty"));
        return;
    }

    if (!m_layoutService) {
        Q_EMIT layoutLoadFailed(i18n("Layout service not initialized"));
        return;
    }

    QString jsonLayout = m_layoutService->loadLayout(layoutId);
    if (jsonLayout.isEmpty()) {
        // Error signal already emitted by service
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(jsonLayout.toUtf8());
    if (doc.isNull() || !doc.isObject()) {
        Q_EMIT layoutLoadFailed(i18n("Invalid layout data format"));
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
    m_availableScreenNames.clear();
    m_virtualDesktopCount = 1;
    m_virtualDesktopNames.clear();
    m_activitiesAvailable = false;
    m_availableActivities.clear();
    {
        QDBusInterface iface{QString(DBus::ServiceName), QString(DBus::ObjectPath),
                             QString(DBus::Interface::LayoutManager)};
        if (iface.isValid()) {
            // Screen names
            QDBusReply<QString> screensReply = iface.call(QStringLiteral("getAllScreenAssignments"));
            if (screensReply.isValid()) {
                QJsonDocument screensDoc = QJsonDocument::fromJson(screensReply.value().toUtf8());
                m_availableScreenNames = screensDoc.object().keys();
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
    int oldOuterGap = m_outerGap;
    m_zonePadding = layoutObj.contains(QLatin1String(JsonKeys::ZonePadding))
        ? layoutObj[QLatin1String(JsonKeys::ZonePadding)].toInt(-1)
        : -1;
    m_outerGap = layoutObj.contains(QLatin1String(JsonKeys::OuterGap))
        ? layoutObj[QLatin1String(JsonKeys::OuterGap)].toInt(-1)
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
        m_cachedShaderParameters = info.value(QLatin1String("parameters")).toList();
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
    Q_EMIT isNewLayoutChanged();
    Q_EMIT hasUnsavedChangesChanged();
    Q_EMIT currentShaderIdChanged();
    Q_EMIT currentShaderParamsChanged();
    Q_EMIT currentShaderParametersChanged();

    // Emit gap change signals if values changed
    if (m_zonePadding != oldZonePadding) {
        Q_EMIT zonePaddingChanged();
    }
    if (m_outerGap != oldOuterGap) {
        Q_EMIT outerGapChanged();
    }

    // Emit visibility filtering signals
    Q_EMIT allowedScreensChanged();
    Q_EMIT allowedDesktopsChanged();
    Q_EMIT allowedActivitiesChanged();
    Q_EMIT availableScreenNamesChanged();
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
        Q_EMIT layoutSaveFailed(i18n("Services not initialized"));
        return;
    }

    // Build JSON from current state
    QJsonObject layoutObj;
    layoutObj[QLatin1String(JsonKeys::Id)] = m_layoutId;
    layoutObj[QLatin1String(JsonKeys::Name)] = m_layoutName;
    layoutObj[QLatin1String(JsonKeys::Type)] = 0; // Custom type
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
        Q_EMIT isNewLayoutChanged();
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

// ═══════════════════════════════════════════════════════════════════════════════
// Visibility Filtering (Tier 2)
// ═══════════════════════════════════════════════════════════════════════════════

QVariantList EditorController::allowedDesktops() const
{
    QVariantList result;
    for (int d : m_allowedDesktopsInt) {
        result.append(d);
    }
    return result;
}

void EditorController::setAllowedScreens(const QStringList& screens)
{
    if (m_allowedScreens != screens) {
        auto* cmd = new UpdateVisibilityCommand(this, m_allowedScreens, screens, m_allowedDesktopsInt,
                                                m_allowedDesktopsInt, m_allowedActivities, m_allowedActivities);
        m_undoController->push(cmd);
    }
}

void EditorController::setAllowedDesktops(const QVariantList& desktops)
{
    QList<int> intList;
    for (const QVariant& v : desktops) {
        intList.append(v.toInt());
    }
    if (m_allowedDesktopsInt != intList) {
        auto* cmd = new UpdateVisibilityCommand(this, m_allowedScreens, m_allowedScreens, m_allowedDesktopsInt,
                                                intList, m_allowedActivities, m_allowedActivities);
        m_undoController->push(cmd);
    }
}

void EditorController::setAllowedActivities(const QStringList& activities)
{
    if (m_allowedActivities != activities) {
        auto* cmd = new UpdateVisibilityCommand(this, m_allowedScreens, m_allowedScreens, m_allowedDesktopsInt,
                                                m_allowedDesktopsInt, m_allowedActivities, activities);
        m_undoController->push(cmd);
    }
}

void EditorController::setAllowedScreensDirect(const QStringList& screens)
{
    if (m_allowedScreens != screens) {
        m_allowedScreens = screens;
        markUnsaved();
        Q_EMIT allowedScreensChanged();
    }
}

void EditorController::setAllowedDesktopsDirect(const QList<int>& desktops)
{
    if (m_allowedDesktopsInt != desktops) {
        m_allowedDesktopsInt = desktops;
        markUnsaved();
        Q_EMIT allowedDesktopsChanged();
    }
}

void EditorController::setAllowedActivitiesDirect(const QStringList& activities)
{
    if (m_allowedActivities != activities) {
        m_allowedActivities = activities;
        markUnsaved();
        Q_EMIT allowedActivitiesChanged();
    }
}

void EditorController::toggleScreenAllowed(const QString& screenName)
{
    QStringList screens = m_allowedScreens;

    if (screens.isEmpty()) {
        // Currently "all screens" - populate with all except this one
        for (const QString& s : m_availableScreenNames) {
            if (s != screenName) {
                screens.append(s);
            }
        }
        // If result is empty (single screen), nothing to restrict
        if (screens.isEmpty()) {
            return;
        }
    } else if (screens.contains(screenName)) {
        screens.removeAll(screenName);
        // If removing last screen, clear to mean "all screens"
        if (screens.isEmpty()) {
            screens.clear(); // explicit: empty = visible everywhere
        }
    } else {
        screens.append(screenName);
        // If all screens are now in the list, clear it (= visible everywhere)
        if (screens.size() >= m_availableScreenNames.size()) {
            screens.clear();
        }
    }

    setAllowedScreens(screens);
}

void EditorController::toggleDesktopAllowed(int desktop)
{
    QList<int> desktops = m_allowedDesktopsInt;

    if (desktops.isEmpty()) {
        // Currently "all desktops" - populate with all except this one
        for (int i = 1; i <= m_virtualDesktopCount; ++i) {
            if (i != desktop) {
                desktops.append(i);
            }
        }
        // If result is empty (single desktop), nothing to restrict
        if (desktops.isEmpty()) {
            return;
        }
    } else if (desktops.contains(desktop)) {
        desktops.removeAll(desktop);
    } else {
        desktops.append(desktop);
        // If all desktops are now in the list, clear it (= visible everywhere)
        if (desktops.size() >= m_virtualDesktopCount) {
            desktops.clear();
        }
    }

    QVariantList varList;
    for (int d : desktops) {
        varList.append(d);
    }
    setAllowedDesktops(varList);
}

void EditorController::toggleActivityAllowed(const QString& activityId)
{
    QStringList activities = m_allowedActivities;

    if (activities.isEmpty()) {
        // Currently "all activities" - populate with all except this one
        for (const QVariant& v : m_availableActivities) {
            QVariantMap actMap = v.toMap();
            QString id = actMap.value(QLatin1String("id")).toString();
            if (id != activityId) {
                activities.append(id);
            }
        }
        // If result is empty (single activity), nothing to restrict
        if (activities.isEmpty()) {
            return;
        }
    } else if (activities.contains(activityId)) {
        activities.removeAll(activityId);
    } else {
        activities.append(activityId);
        // If all activities are now in the list, clear it (= visible everywhere)
        if (activities.size() >= m_availableActivities.size()) {
            activities.clear();
        }
    }

    setAllowedActivities(activities);
}

/**
 * @brief Adds a new zone to the layout
 * @param x Relative X position (0.0-1.0)
 * @param y Relative Y position (0.0-1.0)
 * @param width Relative width (0.0-1.0)
 * @param height Relative height (0.0-1.0)
 * @return Zone ID of the created zone, or empty string on failure
 */
QString EditorController::addZone(qreal x, qreal y, qreal width, qreal height)
{
    if (!m_undoController || !m_zoneManager || !m_snappingService) {
        qCWarning(lcEditor) << "Services not initialized";
        return QString();
    }

    // Input validation
    if (x < 0.0 || x > 1.0 || y < 0.0 || y > 1.0 || width <= 0.0 || width > 1.0 || height <= 0.0 || height > 1.0) {
        qCWarning(lcEditor) << "Invalid zone geometry:" << x << y << width << height;
        return QString();
    }

    // Apply snapping using SnappingService
    QVariantList allZones = m_zoneManager->zones();
    QVariantMap snapped = m_snappingService->snapGeometry(x, y, width, height, allZones);
    x = snapped[JsonKeys::X].toDouble();
    y = snapped[JsonKeys::Y].toDouble();
    width = snapped[JsonKeys::Width].toDouble();
    height = snapped[JsonKeys::Height].toDouble();

    // Minimum size check
    width = qMax(EditorConstants::MinZoneSize, width);
    height = qMax(EditorConstants::MinZoneSize, height);

    // Clamp to screen bounds
    x = qBound(0.0, x, 1.0 - width);
    y = qBound(0.0, y, 1.0 - height);

    // Perform operation first to get zone ID
    QString zoneId = m_zoneManager->addZone(x, y, width, height);
    if (zoneId.isEmpty()) {
        return QString();
    }

    // Get complete zone data for undo command
    QVariantMap zoneData = m_zoneManager->getZoneById(zoneId);
    if (zoneData.isEmpty()) {
        qCWarning(lcEditor) << "Failed to get zone data after creation:" << zoneId;
        return QString();
    }

    // Create and push command (redo() will restore the zone if undone/redone)
    auto* command = new AddZoneCommand(QPointer<ZoneManager>(m_zoneManager), zoneId, zoneData, QString());
    m_undoController->push(command);

    // Select the new zone
    m_selectedZoneId = zoneId;
    Q_EMIT selectedZoneIdChanged();
    markUnsaved();

    return zoneId;
}

/**
 * @brief Updates the geometry of a zone
 * @param zoneId The unique identifier of the zone
 * @param x Relative X position (0.0-1.0)
 * @param y Relative Y position (0.0-1.0)
 * @param width Relative width (0.0-1.0)
 * @param height Relative height (0.0-1.0)
 *
 * Applies snapping and validation before updating.
 * Emits zoneGeometryChanged signal on success.
 */
void EditorController::updateZoneGeometry(const QString& zoneId, qreal x, qreal y, qreal width, qreal height,
                                          bool skipSnapping)
{
    if (!m_undoController || !m_zoneManager || !m_snappingService) {
        qCWarning(lcEditor) << "Cannot update zone geometry - services not initialized";
        return;
    }

    // Input validation
    if (zoneId.isEmpty()) {
        qCWarning(lcEditor) << "Empty zone ID for geometry update";
        return;
    }

    if (x < 0.0 || x > 1.0 || y < 0.0 || y > 1.0 || width <= 0.0 || width > 1.0 || height <= 0.0 || height > 1.0) {
        qCWarning(lcEditor) << "Invalid zone geometry:" << x << y << width << height;
        return;
    }

    // Get current geometry for undo state
    QVariantMap zone = m_zoneManager->getZoneById(zoneId);
    if (zone.isEmpty()) {
        qCWarning(lcEditor) << "Zone not found for geometry update:" << zoneId;
        Q_EMIT layoutSaveFailed(i18nc("@info", "Zone not found"));
        return;
    }

    QRectF oldGeometry(zone[JsonKeys::X].toReal(), zone[JsonKeys::Y].toReal(), zone[JsonKeys::Width].toReal(),
                       zone[JsonKeys::Height].toReal());

    // Apply snapping using SnappingService (unless skipSnapping is true, e.g., for keyboard movements)
    if (!skipSnapping) {
        QVariantList allZones = m_zoneManager->zones();
        QVariantMap snapped = m_snappingService->snapGeometry(x, y, width, height, allZones, zoneId);
        x = snapped[JsonKeys::X].toDouble();
        y = snapped[JsonKeys::Y].toDouble();
        width = snapped[JsonKeys::Width].toDouble();
        height = snapped[JsonKeys::Height].toDouble();
    }

    // Minimum size
    width = qMax(EditorConstants::MinZoneSize, width);
    height = qMax(EditorConstants::MinZoneSize, height);

    // Clamp to screen
    x = qBound(0.0, x, 1.0 - width);
    y = qBound(0.0, y, 1.0 - height);

    QRectF newGeometry(x, y, width, height);

    // Check if geometry actually changed (within small tolerance for floating point)
    // This prevents creating undo commands when selection or sync causes no-op updates
    const qreal tolerance = 0.0001; // Very small tolerance for floating point comparison
    if (qAbs(oldGeometry.x() - newGeometry.x()) < tolerance && qAbs(oldGeometry.y() - newGeometry.y()) < tolerance
        && qAbs(oldGeometry.width() - newGeometry.width()) < tolerance
        && qAbs(oldGeometry.height() - newGeometry.height()) < tolerance) {
        // Geometry hasn't actually changed - don't create undo command
        return;
    }

    // Create and push command
    auto* command = new UpdateZoneGeometryCommand(QPointer<ZoneManager>(m_zoneManager), zoneId, oldGeometry,
                                                  newGeometry, QString());
    m_undoController->push(command);
    markUnsaved();
}

/**
 * @brief Updates the name of a zone
 * @param zoneId The unique identifier of the zone
 * @param name The new name for the zone
 */
void EditorController::updateZoneName(const QString& zoneId, const QString& name)
{
    if (!m_undoController || !m_zoneManager) {
        qCWarning(lcEditor) << "Cannot update zone name - undo controller or zone manager is null";
        Q_EMIT zoneNameValidationError(zoneId, i18nc("@info", "Services not initialized"));
        return;
    }

    // Validate zone name
    QString validationError = validateZoneName(zoneId, name);
    if (!validationError.isEmpty()) {
        Q_EMIT zoneNameValidationError(zoneId, validationError);
        return;
    }

    // Get current name for undo state
    QVariantMap zone = m_zoneManager->getZoneById(zoneId);
    if (zone.isEmpty()) {
        qCWarning(lcEditor) << "Zone not found for name update:" << zoneId;
        Q_EMIT zoneNameValidationError(zoneId, i18nc("@info", "Zone not found"));
        return;
    }

    QString oldName = zone[JsonKeys::Name].toString();

    // Create and push command
    auto* command = new UpdateZoneNameCommand(QPointer<ZoneManager>(m_zoneManager), zoneId, oldName, name, QString());
    m_undoController->push(command);
    markUnsaved();
}

/**
 * @brief Updates the number of a zone
 * @param zoneId The unique identifier of the zone
 * @param number The new zone number
 */
void EditorController::updateZoneNumber(const QString& zoneId, int number)
{
    if (!m_undoController || !m_zoneManager) {
        qCWarning(lcEditor) << "Cannot update zone number - undo controller or zone manager is null";
        Q_EMIT zoneNumberValidationError(zoneId, i18nc("@info", "Services not initialized"));
        return;
    }

    // Validate zone number
    QString validationError = validateZoneNumber(zoneId, number);
    if (!validationError.isEmpty()) {
        Q_EMIT zoneNumberValidationError(zoneId, validationError);
        return;
    }

    // Get current zone number for undo state
    QVariantMap zone = m_zoneManager->getZoneById(zoneId);
    if (zone.isEmpty()) {
        qCWarning(lcEditor) << "Zone not found for number update:" << zoneId;
        Q_EMIT zoneNumberValidationError(zoneId, i18nc("@info", "Zone not found"));
        return;
    }

    int oldNumber = zone[JsonKeys::ZoneNumber].toInt();

    // Create and push command
    auto* command =
        new UpdateZoneNumberCommand(QPointer<ZoneManager>(m_zoneManager), zoneId, oldNumber, number, QString());
    m_undoController->push(command);

    markUnsaved();
}

/**
 * @brief Updates a color property of a zone
 * @param zoneId The unique identifier of the zone
 * @param colorType The color property to update (highlightColor, inactiveColor, borderColor)
 * @param color The new color value (hex string)
 */
void EditorController::updateZoneColor(const QString& zoneId, const QString& colorType, const QString& color)
{
    if (!servicesReady("update zone color")) {
        return;
    }

    // Get current value for undo state
    QVariantMap zone = m_zoneManager->getZoneById(zoneId);
    if (zone.isEmpty()) {
        qCWarning(lcEditor) << "Zone not found for color update:" << zoneId;
        return;
    }

    QVariant oldValue = zone.value(colorType);

    // Create and push command (UpdateZoneAppearanceCommand handles color properties)
    auto* command = new UpdateZoneAppearanceCommand(QPointer<ZoneManager>(m_zoneManager), zoneId, colorType, oldValue,
                                                    color, QString());
    m_undoController->push(command);

    markUnsaved();
}

void EditorController::updateZoneAppearance(const QString& zoneId, const QString& propertyName, const QVariant& value)
{
    if (!servicesReady("update zone appearance")) {
        return;
    }

    // Get current value for undo state
    QVariantMap zone = m_zoneManager->getZoneById(zoneId);
    if (zone.isEmpty()) {
        qCWarning(lcEditor) << "Zone not found for appearance update:" << zoneId;
        return;
    }

    QVariant oldValue = zone.value(propertyName);

    // Create and push command
    auto* command = new UpdateZoneAppearanceCommand(QPointer<ZoneManager>(m_zoneManager), zoneId, propertyName,
                                                    oldValue, value, QString());
    m_undoController->push(command);
    markUnsaved();
}

/**
 * @brief Deletes a zone from the layout
 * @param zoneId The unique identifier of the zone to delete
 */
void EditorController::deleteZone(const QString& zoneId)
{
    if (!servicesReady("delete zone")) {
        return;
    }

    // Get zone data for undo
    QVariantMap zoneData = m_zoneManager->getZoneById(zoneId);
    if (zoneData.isEmpty()) {
        qCWarning(lcEditor) << "Zone not found for deletion:" << zoneId;
        Q_EMIT layoutSaveFailed(i18nc("@info", "Zone not found"));
        return;
    }

    // Create and push command
    auto* command = new DeleteZoneCommand(QPointer<ZoneManager>(m_zoneManager), zoneId, zoneData, QString());
    m_undoController->push(command);

    // Update selection state
    if (m_selectedZoneIds.contains(zoneId)) {
        m_selectedZoneIds.removeAll(zoneId);
        syncSelectionSignals();
    }

    markUnsaved();
}

/**
 * @brief Find zones adjacent to the given zone
 * @param zoneId The zone to find neighbors for
 * @return Map with "left", "right", "top", "bottom" lists of adjacent zone IDs
 */
QVariantMap EditorController::findAdjacentZones(const QString& zoneId)
{
    if (!m_zoneManager) {
        qCWarning(lcEditor) << "ZoneManager not initialized";
        return QVariantMap();
    }

    return m_zoneManager->findAdjacentZones(zoneId);
}

/**
 * @brief Expand a zone to fill available empty space around it
 * @param zoneId The zone to expand
 * @param mouseX Normalized mouse X position (0-1), or -1 to use zone center
 * @param mouseY Normalized mouse Y position (0-1), or -1 to use zone center
 * @return true if any expansion occurred
 */
bool EditorController::expandToFillSpace(const QString& zoneId, qreal mouseX, qreal mouseY)
{
    if (!servicesReady("expand zone")) {
        return false;
    }

    // Get old geometry for undo state
    QVariantMap zone = m_zoneManager->getZoneById(zoneId);
    if (zone.isEmpty()) {
        qCWarning(lcEditor) << "Zone not found for fill:" << zoneId;
        return false;
    }

    QRectF oldGeometry(zone[JsonKeys::X].toReal(), zone[JsonKeys::Y].toReal(), zone[JsonKeys::Width].toReal(),
                       zone[JsonKeys::Height].toReal());

    // Perform operation
    bool result = m_zoneManager->expandToFillSpace(zoneId, mouseX, mouseY);
    if (!result) {
        return false;
    }

    // Get new geometry after operation
    QVariantMap updatedZone = m_zoneManager->getZoneById(zoneId);
    if (updatedZone.isEmpty()) {
        return false;
    }

    QRectF newGeometry(updatedZone[JsonKeys::X].toReal(), updatedZone[JsonKeys::Y].toReal(),
                       updatedZone[JsonKeys::Width].toReal(), updatedZone[JsonKeys::Height].toReal());

    // Create and push command
    auto* command =
        new FillZoneCommand(QPointer<ZoneManager>(m_zoneManager), zoneId, oldGeometry, newGeometry, QString());
    m_undoController->push(command);

    markUnsaved();
    return true;
}

QVariantMap EditorController::calculateFillRegion(const QString& zoneId, qreal mouseX, qreal mouseY)
{
    if (!m_zoneManager) {
        return QVariantMap();
    }
    return m_zoneManager->calculateFillRegion(zoneId, mouseX, mouseY);
}

/**
 * @brief Delete a zone and optionally expand neighbors to fill the gap
 * @param zoneId The zone to delete
 * @param autoFill If true, expand adjacent zones to fill the deleted zone's space
 */
void EditorController::deleteZoneWithFill(const QString& zoneId, bool autoFill)
{
    if (!servicesReady("delete zone with fill")) {
        return;
    }

    // Get old zones list before operation
    QVariantList oldZones = m_zoneManager->zones();

    // Get deleted zone data
    QVariantMap deletedZoneData = m_zoneManager->getZoneById(zoneId);
    if (deletedZoneData.isEmpty()) {
        qCWarning(lcEditor) << "Zone not found for deletion with fill:" << zoneId;
        return;
    }

    // Perform operation
    m_zoneManager->deleteZoneWithFill(zoneId, autoFill);

    // Get new zones list after operation
    QVariantList newZones = m_zoneManager->zones();

    // Create and push command
    auto* command = new DeleteZoneWithFillCommand(QPointer<ZoneManager>(m_zoneManager), zoneId, deletedZoneData,
                                                  oldZones, newZones, QString());
    m_undoController->push(command);

    // Update selection state
    if (m_selectedZoneIds.contains(zoneId)) {
        m_selectedZoneIds.removeAll(zoneId);
        syncSelectionSignals();
    }

    markUnsaved();
}

// ═══════════════════════════════════════════════════════════════════════════
// Z-ORDER OPERATIONS
// ═══════════════════════════════════════════════════════════════════════════

void EditorController::changeZOrderImpl(const QString& zoneId, ZOrderOp op, const QString& actionName)
{
    if (!servicesReady("change z-order")) {
        return;
    }

    QVariantList oldZones = m_zoneManager->zones();

    switch (op) {
    case ZOrderOp::BringToFront:
        m_zoneManager->bringToFront(zoneId);
        break;
    case ZOrderOp::SendToBack:
        m_zoneManager->sendToBack(zoneId);
        break;
    case ZOrderOp::BringForward:
        m_zoneManager->bringForward(zoneId);
        break;
    case ZOrderOp::SendBackward:
        m_zoneManager->sendBackward(zoneId);
        break;
    }

    QVariantList newZones = m_zoneManager->zones();
    auto* command = new ChangeZOrderCommand(QPointer<ZoneManager>(m_zoneManager), zoneId, oldZones, newZones, actionName);
    m_undoController->push(command);
    markUnsaved();
}

void EditorController::bringToFront(const QString& zoneId)
{
    changeZOrderImpl(zoneId, ZOrderOp::BringToFront, i18nc("@action", "Bring to Front"));
}

void EditorController::sendToBack(const QString& zoneId)
{
    changeZOrderImpl(zoneId, ZOrderOp::SendToBack, i18nc("@action", "Send to Back"));
}

void EditorController::bringForward(const QString& zoneId)
{
    changeZOrderImpl(zoneId, ZOrderOp::BringForward, i18nc("@action", "Bring Forward"));
}

void EditorController::sendBackward(const QString& zoneId)
{
    changeZOrderImpl(zoneId, ZOrderOp::SendBackward, i18nc("@action", "Send Backward"));
}

/**
 * @brief Creates a duplicate of an existing zone
 * @param zoneId The unique identifier of the zone to duplicate
 * @return Zone ID of the new zone, or empty string on failure
 */
QString EditorController::duplicateZone(const QString& zoneId)
{
    if (!servicesReady("duplicate zone")) {
        return QString();
    }

    // Get source zone data BEFORE operation (for command state)
    QVariantMap sourceZoneData = m_zoneManager->getZoneById(zoneId);
    if (sourceZoneData.isEmpty()) {
        qCWarning(lcEditor) << "Source zone not found for duplication:" << zoneId;
        return QString();
    }

    // Calculate duplicate zone data (offset position, new ID will be generated in redo())
    qreal x = sourceZoneData[JsonKeys::X].toDouble();
    qreal y = sourceZoneData[JsonKeys::Y].toDouble();
    qreal width = sourceZoneData[JsonKeys::Width].toDouble();
    qreal height = sourceZoneData[JsonKeys::Height].toDouble();
    QString sourceName = sourceZoneData[JsonKeys::Name].toString();

    // Calculate offset position
    qreal newX = x + EditorConstants::DuplicateOffset;
    qreal newY = y + EditorConstants::DuplicateOffset;
    newX = qMin(newX, 1.0 - width);
    newY = qMin(newY, 1.0 - height);

    // Create duplicate zone data (new ID will be generated in redo())
    QVariantMap duplicatedZoneData = sourceZoneData;
    duplicatedZoneData[JsonKeys::Id] = QString(); // Empty ID - will be generated in redo()
    duplicatedZoneData[JsonKeys::X] = newX;
    duplicatedZoneData[JsonKeys::Y] = newY;
    duplicatedZoneData[JsonKeys::Name] = QString(sourceName + QStringLiteral(" (Copy)"));

    // Create command (redo() will perform the operation)
    // We need to get the new zone ID after redo() is called, so we'll use a temporary approach
    // Actually, we need the zone ID to select it, so we'll perform the operation and then push
    // But we need to make redo() idempotent

    // Perform operation to get zone ID for selection
    QString newZoneId = m_zoneManager->duplicateZone(zoneId);
    if (newZoneId.isEmpty()) {
        return QString();
    }

    // Get the actual duplicated zone data (with the generated ID)
    QVariantMap actualDuplicatedZoneData = m_zoneManager->getZoneById(newZoneId);
    if (actualDuplicatedZoneData.isEmpty()) {
        qCWarning(lcEditor) << "Failed to get duplicated zone data";
        return QString();
    }

    // Update the zone data with the actual ID
    duplicatedZoneData[JsonKeys::Id] = newZoneId;

    // Create and push command (redo() will be called automatically, but zone already exists)
    auto* command = new DuplicateZoneCommand(QPointer<ZoneManager>(m_zoneManager), zoneId, newZoneId,
                                             duplicatedZoneData, QString());
    m_undoController->push(command);

    if (!newZoneId.isEmpty()) {
        m_selectedZoneId = newZoneId;
        Q_EMIT selectedZoneIdChanged();
        markUnsaved();
    }

    return newZoneId;
}

/**
 * @brief Applies a template layout to the editor
 * @param templateType The type of template (grid, columns, rows, priority, focus)
 * @param columns Number of columns (for grid/columns templates)
 * @param rows Number of rows (for grid/rows templates)
 *
 * Clears existing zones and creates new zones based on the template.
 * Validates input parameters and uses default values if invalid.
 */
void EditorController::applyTemplate(const QString& templateType, int columns, int rows)
{
    if (!m_undoController || !m_templateService || !m_zoneManager) {
        qCWarning(lcEditor) << "Services not initialized";
        return;
    }

    // Get old zones for undo
    QVariantList oldZones = m_zoneManager->zones();

    QVariantList zones = m_templateService->applyTemplate(templateType, columns, rows);
    if (zones.isEmpty()) {
        qCWarning(lcEditor) << "Template application failed for" << templateType;
        return;
    }

    // Update template zones to use theme-based default colors if they're using hardcoded defaults
    QString defaultHighlight = m_defaultHighlightColor.isEmpty()
        ? QString::fromLatin1(EditorConstants::DefaultHighlightColor)
        : m_defaultHighlightColor;
    QString defaultInactive = m_defaultInactiveColor.isEmpty()
        ? QString::fromLatin1(EditorConstants::DefaultInactiveColor)
        : m_defaultInactiveColor;
    QString defaultBorder = m_defaultBorderColor.isEmpty() ? QString::fromLatin1(EditorConstants::DefaultBorderColor)
                                                           : m_defaultBorderColor;

    for (QVariant& zoneVar : zones) {
        QVariantMap zone = zoneVar.toMap();
        // Only update if using the old hardcoded defaults
        QString currentHighlight = zone[JsonKeys::HighlightColor].toString();
        QString currentInactive = zone[JsonKeys::InactiveColor].toString();
        QString currentBorder = zone[JsonKeys::BorderColor].toString();

        if (currentHighlight == QLatin1String(EditorConstants::DefaultHighlightColor)) {
            zone[JsonKeys::HighlightColor] = defaultHighlight;
        }
        if (currentInactive == QLatin1String(EditorConstants::DefaultInactiveColor)) {
            zone[JsonKeys::InactiveColor] = defaultInactive;
        }
        if (currentBorder == QLatin1String(EditorConstants::DefaultBorderColor)) {
            zone[JsonKeys::BorderColor] = defaultBorder;
        }
        zoneVar = zone;
    }

    // Create and push command
    auto* command =
        new ApplyTemplateCommand(QPointer<ZoneManager>(m_zoneManager), templateType, oldZones, zones, QString());
    m_undoController->push(command);

    m_selectedZoneId.clear();
    m_selectedZoneIds.clear();
    Q_EMIT selectedZoneIdChanged();
    Q_EMIT selectedZoneIdsChanged();
    markUnsaved();
}

/**
 * @brief Removes all zones from the layout
 *
 * Clears the zones list and deselects any selected zone.
 */
void EditorController::clearAllZones()
{
    if (!servicesReady("clear zones")) {
        return;
    }

    // Get old zones for undo
    QVariantList oldZones = m_zoneManager->zones();

    // Create and push command
    auto* command = new ClearAllZonesCommand(QPointer<ZoneManager>(m_zoneManager), oldZones, QString());
    m_undoController->push(command);

    m_selectedZoneId.clear();
    m_selectedZoneIds.clear();
    Q_EMIT selectedZoneIdChanged();
    Q_EMIT selectedZoneIdsChanged();
    markUnsaved();
}

/**
 * @brief Snaps geometry to grid and/or zone edges
 * @param x Relative X position (0.0-1.0)
 * @param y Relative Y position (0.0-1.0)
 * @param width Relative width (0.0-1.0)
 * @param height Relative height (0.0-1.0)
 * @param excludeZoneId Zone ID to exclude from edge snapping
 * @return QVariantMap with snapped x, y, width, height
 *
 * Applies grid and edge snapping based on current settings.
 * Snaps all edges by default.
 */
QVariantMap EditorController::snapGeometry(qreal x, qreal y, qreal width, qreal height, const QString& excludeZoneId)
{
    if (!m_snappingService || !m_zoneManager) {
        // Fallback: return unsnapped geometry
        QVariantMap result;
        result[JsonKeys::X] = x;
        result[JsonKeys::Y] = y;
        result[JsonKeys::Width] = width;
        result[JsonKeys::Height] = height;
        return result;
    }

    QVariantList allZones = m_zoneManager->zones();
    return m_snappingService->snapGeometry(x, y, width, height, allZones, excludeZoneId);
}

/**
 * @brief Snaps geometry selectively (only specified edges)
 * @param x Relative X position (0.0-1.0)
 * @param y Relative Y position (0.0-1.0)
 * @param width Relative width (0.0-1.0)
 * @param height Relative height (0.0-1.0)
 * @param excludeZoneId Zone ID to exclude from edge snapping
 * @param snapLeft Whether to snap the left edge
 * @param snapRight Whether to snap the right edge
 * @param snapTop Whether to snap the top edge
 * @param snapBottom Whether to snap the bottom edge
 * @return QVariantMap with snapped x, y, width, height
 *
 * Used during resize operations to only snap the edge being moved.
 */
QVariantMap EditorController::snapGeometrySelective(qreal x, qreal y, qreal width, qreal height,
                                                    const QString& excludeZoneId, bool snapLeft, bool snapRight,
                                                    bool snapTop, bool snapBottom)
{
    if (!m_snappingService || !m_zoneManager) {
        // Fallback: return unsnapped geometry
        QVariantMap result;
        result[JsonKeys::X] = x;
        result[JsonKeys::Y] = y;
        result[JsonKeys::Width] = width;
        result[JsonKeys::Height] = height;
        return result;
    }

    QVariantList allZones = m_zoneManager->zones();
    return m_snappingService->snapGeometrySelective(x, y, width, height, allZones, excludeZoneId, snapLeft, snapRight,
                                                    snapTop, snapBottom);
}

/**
 * @brief Marks the layout as having unsaved changes
 *
 * Internal helper to track modification state.
 * Only emits signal if state actually changes.
 */
void EditorController::markUnsaved()
{
    if (!m_hasUnsavedChanges) {
        m_hasUnsavedChanges = true;
        Q_EMIT hasUnsavedChangesChanged();
    }
}

bool EditorController::servicesReady(const char* operation) const
{
    if (!m_undoController || !m_zoneManager) {
        qCWarning(lcEditor) << "Cannot" << operation << "- undo controller or zone manager is null";
        return false;
    }
    return true;
}

void EditorController::syncSelectionSignals()
{
    QString newSelectedId = m_selectedZoneIds.isEmpty() ? QString() : m_selectedZoneIds.first();
    if (m_selectedZoneId != newSelectedId) {
        m_selectedZoneId = newSelectedId;
        Q_EMIT selectedZoneIdChanged();
    }
    Q_EMIT selectedZoneIdsChanged();
}

/**
 * @brief Selects the next zone in the zone list
 * @return Zone ID of the newly selected zone, or empty string if no zones
 */
QString EditorController::selectNextZone()
{
    if (!m_zoneManager) {
        return QString();
    }

    QVariantList zones = m_zoneManager->zones();
    if (zones.isEmpty()) {
        return QString();
    }

    // Find current zone index
    int currentIndex = -1;
    if (!m_selectedZoneId.isEmpty()) {
        currentIndex = m_zoneManager->findZoneIndex(m_selectedZoneId);
    }

    // Select next zone (wrap around to first if at end)
    int nextIndex = (currentIndex + 1) % zones.length();
    QVariantMap nextZone = zones[nextIndex].toMap();
    QString nextZoneId = nextZone[JsonKeys::Id].toString();

    setSelectedZoneId(nextZoneId);
    return nextZoneId;
}

/**
 * @brief Selects the previous zone in the zone list
 * @return Zone ID of the newly selected zone, or empty string if no zones
 */
QString EditorController::selectPreviousZone()
{
    if (!m_zoneManager) {
        return QString();
    }

    QVariantList zones = m_zoneManager->zones();
    if (zones.isEmpty()) {
        return QString();
    }

    // Find current zone index
    int currentIndex = -1;
    if (!m_selectedZoneId.isEmpty()) {
        currentIndex = m_zoneManager->findZoneIndex(m_selectedZoneId);
    }

    // Select previous zone (wrap around to last if at beginning)
    int prevIndex = currentIndex <= 0 ? zones.length() - 1 : currentIndex - 1;
    QVariantMap prevZone = zones[prevIndex].toMap();
    QString prevZoneId = prevZone[JsonKeys::Id].toString();

    setSelectedZoneId(prevZoneId);
    return prevZoneId;
}

/**
 * @brief Moves the selected zone in the specified direction
 * @param direction 0=left, 1=right, 2=up, 3=down
 * @param step Movement step size (relative, default 0.01 = 1%)
 * @return true if zone was moved, false if no zone selected or invalid direction
 */
bool EditorController::moveSelectedZone(int direction, qreal step)
{
    if (m_selectedZoneId.isEmpty() || !m_zoneManager) {
        return false;
    }

    QVariantList zones = m_zoneManager->zones();
    QVariantMap selectedZone;
    for (const QVariant& zoneVar : zones) {
        QVariantMap zone = zoneVar.toMap();
        if (zone[JsonKeys::Id].toString() == m_selectedZoneId) {
            selectedZone = zone;
            break;
        }
    }

    if (selectedZone.isEmpty()) {
        return false;
    }

    qreal x = selectedZone[JsonKeys::X].toDouble();
    qreal y = selectedZone[JsonKeys::Y].toDouble();
    qreal width = selectedZone[JsonKeys::Width].toDouble();
    qreal height = selectedZone[JsonKeys::Height].toDouble();

    // Apply movement based on direction
    switch (direction) {
    case 0: // Left
        x = qMax(0.0, x - step);
        break;
    case 1: // Right
        x = qMin(1.0 - width, x + step);
        break;
    case 2: // Up
        y = qMax(0.0, y - step);
        break;
    case 3: // Down
        y = qMin(1.0 - height, y + step);
        break;
    default:
        return false;
    }

    // Clamp to valid bounds
    x = qBound(0.0, x, 1.0 - width);
    y = qBound(0.0, y, 1.0 - height);

    // Update zone geometry (skip snapping for keyboard movements)
    updateZoneGeometry(m_selectedZoneId, x, y, width, height, true);
    return true;
}

/**
 * @brief Resizes the selected zone in the specified direction
 * @param direction 0=left (grow left), 1=right (grow right), 2=top (grow top), 3=bottom (grow bottom)
 * @param step Resize step size (relative, default 0.01 = 1%)
 * @return true if zone was resized, false if no zone selected or invalid direction
 */
bool EditorController::resizeSelectedZone(int direction, qreal step)
{
    if (m_selectedZoneId.isEmpty() || !m_zoneManager) {
        return false;
    }

    QVariantList zones = m_zoneManager->zones();
    QVariantMap selectedZone;
    for (const QVariant& zoneVar : zones) {
        QVariantMap zone = zoneVar.toMap();
        if (zone[JsonKeys::Id].toString() == m_selectedZoneId) {
            selectedZone = zone;
            break;
        }
    }

    if (selectedZone.isEmpty()) {
        return false;
    }

    qreal x = selectedZone[JsonKeys::X].toDouble();
    qreal y = selectedZone[JsonKeys::Y].toDouble();
    qreal width = selectedZone[JsonKeys::Width].toDouble();
    qreal height = selectedZone[JsonKeys::Height].toDouble();

    const qreal minSize = 0.05; // Minimum 5% size

    // Apply resize based on direction
    // Left/Up = shrink, Right/Down = grow (intuitive behavior)
    switch (direction) {
    case 0: // Left (shrink width)
        width = qMax(minSize, width - step);
        break;
    case 1: // Right (grow width)
        width = qMin(1.0 - x, width + step);
        break;
    case 2: // Up (shrink height)
        height = qMax(minSize, height - step);
        break;
    case 3: // Down (grow height)
        height = qMin(1.0 - y, height + step);
        break;
    default:
        return false;
    }

    // Ensure minimum size
    if (width < minSize)
        width = minSize;
    if (height < minSize)
        height = minSize;

    // Clamp to valid bounds
    if (x + width > 1.0) {
        width = 1.0 - x;
        if (width < minSize) {
            width = minSize;
            x = 1.0 - minSize;
        }
    }
    if (y + height > 1.0) {
        height = 1.0 - y;
        if (height < minSize) {
            height = minSize;
            y = 1.0 - minSize;
        }
    }

    // Update zone geometry (skip snapping for keyboard resizes)
    updateZoneGeometry(m_selectedZoneId, x, y, width, height, true);
    return true;
}

// ============================================================================
// Multi-selection manipulation methods
// ============================================================================

void EditorController::addToSelection(const QString& zoneId)
{
    if (zoneId.isEmpty() || m_selectedZoneIds.contains(zoneId)) {
        return;
    }

    // Verify zone exists
    if (m_zoneManager && m_zoneManager->getZoneById(zoneId).isEmpty()) {
        return;
    }

    m_selectedZoneIds.append(zoneId);

    // Update single selection to first if this is the first zone
    if (m_selectedZoneIds.count() == 1) {
        m_selectedZoneId = zoneId;
        Q_EMIT selectedZoneIdChanged();
    }

    Q_EMIT selectedZoneIdsChanged();
}

void EditorController::removeFromSelection(const QString& zoneId)
{
    if (!m_selectedZoneIds.contains(zoneId)) {
        return;
    }

    m_selectedZoneIds.removeAll(zoneId);
    syncSelectionSignals();
}

void EditorController::toggleSelection(const QString& zoneId)
{
    if (m_selectedZoneIds.contains(zoneId)) {
        removeFromSelection(zoneId);
    } else {
        addToSelection(zoneId);
    }
}

void EditorController::selectRange(const QString& fromId, const QString& toId)
{
    if (!m_zoneManager || fromId.isEmpty() || toId.isEmpty()) {
        return;
    }

    QVariantList allZones = m_zoneManager->zones();
    int fromIndex = -1;
    int toIndex = -1;

    // Find indices of both zones
    for (int i = 0; i < allZones.count(); ++i) {
        QVariantMap zone = allZones[i].toMap();
        QString id = zone[JsonKeys::Id].toString();
        if (id == fromId)
            fromIndex = i;
        if (id == toId)
            toIndex = i;
    }

    if (fromIndex < 0 || toIndex < 0) {
        return;
    }

    // Ensure from < to
    if (fromIndex > toIndex) {
        std::swap(fromIndex, toIndex);
    }

    // Select all zones in range (adds to existing selection)
    for (int i = fromIndex; i <= toIndex; ++i) {
        QVariantMap zone = allZones[i].toMap();
        QString zoneId = zone[JsonKeys::Id].toString();
        if (!m_selectedZoneIds.contains(zoneId)) {
            m_selectedZoneIds.append(zoneId);
        }
    }

    // Update single selection for backward compatibility
    if (!m_selectedZoneIds.isEmpty() && m_selectedZoneId != m_selectedZoneIds.first()) {
        m_selectedZoneId = m_selectedZoneIds.first();
        Q_EMIT selectedZoneIdChanged();
    }

    Q_EMIT selectedZoneIdsChanged();
}

void EditorController::selectAll()
{
    if (!m_zoneManager) {
        return;
    }

    QVariantList allZones = m_zoneManager->zones();
    QStringList newSelection;

    for (const QVariant& zoneVar : allZones) {
        QVariantMap zone = zoneVar.toMap();
        newSelection.append(zone[JsonKeys::Id].toString());
    }

    setSelectedZoneIds(newSelection);
}

void EditorController::clearSelection()
{
    if (m_selectedZoneIds.isEmpty()) {
        return;
    }

    m_selectedZoneIds.clear();
    if (!m_selectedZoneId.isEmpty()) {
        m_selectedZoneId.clear();
        Q_EMIT selectedZoneIdChanged();
    }
    Q_EMIT selectedZoneIdsChanged();
}

bool EditorController::isSelected(const QString& zoneId) const
{
    return m_selectedZoneIds.contains(zoneId);
}

bool EditorController::allSelectedUseCustomColors() const
{
    if (!m_zoneManager || m_selectedZoneIds.isEmpty()) {
        return false;
    }
    
    for (const QString& zoneId : m_selectedZoneIds) {
        const QVariantMap zone = m_zoneManager->getZoneById(zoneId);
        if (zone.isEmpty()) {
            return false;
        }
        // Check useCustomColors property (JsonKeys::UseCustomColors is already QLatin1String)
        if (!zone.value(QString(JsonKeys::UseCustomColors), false).toBool()) {
            return false;
        }
    }
    return true;
}

QStringList EditorController::selectZonesInRect(qreal x, qreal y, qreal width, qreal height, bool additive)
{
    if (!m_zoneManager || width <= 0.0 || height <= 0.0) {
        return QStringList();
    }
    
    const qreal rectRight = x + width;
    const qreal rectBottom = y + height;
    
    // Start with existing selection if additive
    QStringList selectedIds = additive ? m_selectedZoneIds : QStringList();
    
    // Get zones and check intersection
    const QVariantList& zonesList = m_zoneManager->zones();
    for (const QVariant& zoneVar : zonesList) {
        const QVariantMap zone = zoneVar.toMap();
        const QString zoneId = zone.value(QString(JsonKeys::Id)).toString();
        if (zoneId.isEmpty()) {
            continue;
        }
        
        // Zone bounds
        const qreal zoneX = zone.value(QString(JsonKeys::X)).toDouble();
        const qreal zoneY = zone.value(QString(JsonKeys::Y)).toDouble();
        const qreal zoneRight = zoneX + zone.value(QString(JsonKeys::Width)).toDouble();
        const qreal zoneBottom = zoneY + zone.value(QString(JsonKeys::Height)).toDouble();
        
        // Check AABB intersection
        const bool intersects = !(zoneRight < x || zoneX > rectRight || 
                                  zoneBottom < y || zoneY > rectBottom);
        
        if (intersects && !selectedIds.contains(zoneId)) {
            selectedIds.append(zoneId);
        }
    }
    
    // Update selection if we found any zones
    if (!selectedIds.isEmpty()) {
        setSelectedZoneIds(selectedIds);
    }
    
    return selectedIds;
}

// ============================================================================
// Batch operations for multi-selection
// ============================================================================

void EditorController::deleteSelectedZones()
{
    if (m_selectedZoneIds.isEmpty() || !m_undoController || !m_zoneManager) {
        return;
    }

    // Copy list since we'll modify it during deletion
    QStringList zonesToDelete = m_selectedZoneIds;

    {
        BatchOperationScope scope(m_undoController, m_zoneManager,
                                  i18nc("@action", "Delete %1 Zones", zonesToDelete.count()));
        for (const QString& zoneId : zonesToDelete) {
            deleteZone(zoneId);
        }
    }

    // Clear selection (already done by deleteZone removing individual zones)
    clearSelection();
}

QStringList EditorController::duplicateSelectedZones()
{
    if (m_selectedZoneIds.isEmpty() || !m_undoController || !m_zoneManager) {
        return QStringList();
    }

    // For single selection, use existing implementation
    if (m_selectedZoneIds.count() == 1) {
        QString newId = duplicateZone(m_selectedZoneIds.first());
        return newId.isEmpty() ? QStringList() : QStringList{newId};
    }

    // Copy selected zones
    QStringList zonesToDuplicate = m_selectedZoneIds;
    QStringList newZoneIds;

    {
        BatchOperationScope scope(m_undoController, m_zoneManager,
                                  i18nc("@action", "Duplicate %1 Zones", zonesToDuplicate.count()));
        for (const QString& zoneId : zonesToDuplicate) {
            QString newId = duplicateZone(zoneId);
            if (!newId.isEmpty()) {
                newZoneIds.append(newId);
            }
        }
    }

    // Select all duplicated zones
    if (!newZoneIds.isEmpty()) {
        setSelectedZoneIds(newZoneIds);
    }

    return newZoneIds;
}

bool EditorController::moveSelectedZones(int direction, qreal step)
{
    if (m_selectedZoneIds.isEmpty() || !m_zoneManager) {
        return false;
    }

    // For single selection, use existing implementation
    if (m_selectedZoneIds.count() == 1) {
        return moveSelectedZone(direction, step);
    }

    // Collect all zone data first
    QList<QPair<QString, QVariantMap>> zonesToMove;
    for (const QString& zoneId : m_selectedZoneIds) {
        QVariantMap zone = m_zoneManager->getZoneById(zoneId);
        if (!zone.isEmpty()) {
            zonesToMove.append({zoneId, zone});
        }
    }

    if (zonesToMove.isEmpty()) {
        return false;
    }

    // Calculate movement deltas based on direction
    qreal dx = 0.0, dy = 0.0;
    switch (direction) {
    case 0:
        dx = -step;
        break; // Left
    case 1:
        dx = step;
        break; // Right
    case 2:
        dy = -step;
        break; // Up
    case 3:
        dy = step;
        break; // Down
    default:
        return false;
    }

    // Check if movement is valid for all zones (no zone goes out of bounds)
    for (const auto& pair : zonesToMove) {
        const QVariantMap& zone = pair.second;
        qreal x = zone[JsonKeys::X].toDouble() + dx;
        qreal y = zone[JsonKeys::Y].toDouble() + dy;
        qreal w = zone[JsonKeys::Width].toDouble();
        qreal h = zone[JsonKeys::Height].toDouble();

        if (x < 0 || y < 0 || x + w > 1.0 || y + h > 1.0) {
            // Adjust to boundary
            if (dx < 0 && x < 0)
                dx = -zone[JsonKeys::X].toDouble();
            if (dx > 0 && x + w > 1.0)
                dx = 1.0 - w - zone[JsonKeys::X].toDouble();
            if (dy < 0 && y < 0)
                dy = -zone[JsonKeys::Y].toDouble();
            if (dy > 0 && y + h > 1.0)
                dy = 1.0 - h - zone[JsonKeys::Y].toDouble();
        }
    }

    // Apply movement using RAII scope for undo macro and batch update
    {
        BatchOperationScope scope(m_undoController, m_zoneManager,
                                  i18nc("@action", "Move %1 Zones", zonesToMove.count()));
        for (const auto& pair : zonesToMove) {
            const QString& zoneId = pair.first;
            const QVariantMap& zone = pair.second;
            qreal x = qBound(0.0, zone[JsonKeys::X].toDouble() + dx, 1.0 - zone[JsonKeys::Width].toDouble());
            qreal y = qBound(0.0, zone[JsonKeys::Y].toDouble() + dy, 1.0 - zone[JsonKeys::Height].toDouble());
            updateZoneGeometry(zoneId, x, y, zone[JsonKeys::Width].toDouble(), zone[JsonKeys::Height].toDouble(),
                               true); // Skip snapping for keyboard
        }
    }
    return true;
}

bool EditorController::resizeSelectedZones(int direction, qreal step)
{
    if (m_selectedZoneIds.isEmpty() || !m_zoneManager) {
        return false;
    }

    // For single selection, use existing implementation
    if (m_selectedZoneIds.count() == 1) {
        return resizeSelectedZone(direction, step);
    }

    // Collect all zone data first
    QList<QPair<QString, QVariantMap>> zonesToResize;
    for (const QString& zoneId : m_selectedZoneIds) {
        QVariantMap zone = m_zoneManager->getZoneById(zoneId);
        if (!zone.isEmpty()) {
            zonesToResize.append({zoneId, zone});
        }
    }

    if (zonesToResize.isEmpty()) {
        return false;
    }

    const qreal minSize = 0.05; // Minimum 5% size

    {
        BatchOperationScope scope(m_undoController, m_zoneManager,
                                  i18nc("@action", "Resize %1 Zones", zonesToResize.count()));
        for (const auto& pair : zonesToResize) {
            const QString& zoneId = pair.first;
            const QVariantMap& zone = pair.second;
            qreal x = zone[JsonKeys::X].toDouble();
            qreal y = zone[JsonKeys::Y].toDouble();
            qreal width = zone[JsonKeys::Width].toDouble();
            qreal height = zone[JsonKeys::Height].toDouble();

            // Apply resize based on direction (same logic as resizeSelectedZone)
            // Left/Up = shrink, Right/Down = grow (intuitive behavior)
            switch (direction) {
            case 0: // Left (shrink width)
                width = qMax(minSize, width - step);
                break;
            case 1: // Right (grow width)
                width = qMin(1.0 - x, width + step);
                break;
            case 2: // Up (shrink height)
                height = qMax(minSize, height - step);
                break;
            case 3: // Down (grow height)
                height = qMin(1.0 - y, height + step);
                break;
            default:
                continue;
            }

            // Ensure minimum size
            if (width < minSize)
                width = minSize;
            if (height < minSize)
                height = minSize;

            // Clamp to valid bounds
            if (x + width > 1.0) {
                width = 1.0 - x;
                if (width < minSize) {
                    width = minSize;
                    x = 1.0 - minSize;
                }
            }
            if (y + height > 1.0) {
                height = 1.0 - y;
                if (height < minSize) {
                    height = minSize;
                    y = 1.0 - minSize;
                }
            }

            updateZoneGeometry(zoneId, x, y, width, height, true); // Skip snapping for keyboard
        }
    }
    return true;
}

// Multi-zone drag operations

void EditorController::startMultiZoneDrag(const QString& primaryZoneId, qreal startX, qreal startY)
{
    if (!m_zoneManager || primaryZoneId.isEmpty()) {
        return;
    }

    // Only activate multi-zone drag if multiple zones are selected and this zone is one of them
    if (m_selectedZoneIds.count() <= 1 || !m_selectedZoneIds.contains(primaryZoneId)) {
        m_multiZoneDragActive = false;
        return;
    }

    m_multiZoneDragActive = true;
    m_dragPrimaryZoneId = primaryZoneId;
    m_dragStartX = startX;
    m_dragStartY = startY;
    m_dragInitialPositions.clear();

    // Store initial positions of all selected zones
    for (const QString& zoneId : m_selectedZoneIds) {
        QVariantMap zone = m_zoneManager->getZoneById(zoneId);
        if (!zone.isEmpty()) {
            qreal x = zone[JsonKeys::X].toDouble();
            qreal y = zone[JsonKeys::Y].toDouble();
            m_dragInitialPositions[zoneId] = QPointF(x, y);
        }
    }
}

void EditorController::updateMultiZoneDrag(const QString& primaryZoneId, qreal newX, qreal newY)
{
    if (!m_multiZoneDragActive || !m_zoneManager || primaryZoneId != m_dragPrimaryZoneId) {
        return;
    }

    // Calculate delta from primary zone's starting position
    qreal dx = newX - m_dragStartX;
    qreal dy = newY - m_dragStartY;

    // Use batch update to defer signals until all zones are updated
    // This prevents QML from rebuilding mid-iteration which causes crashes
    m_zoneManager->beginBatchUpdate();

    // Update visual positions for all other selected zones
    // The primary zone is already being updated by the drag handler
    for (auto it = m_dragInitialPositions.constBegin(); it != m_dragInitialPositions.constEnd(); ++it) {
        if (it.key() == primaryZoneId) {
            continue; // Skip primary zone - it's handled by drag handler
        }

        QVariantMap zone = m_zoneManager->getZoneById(it.key());
        if (zone.isEmpty()) {
            continue;
        }

        qreal width = zone[JsonKeys::Width].toDouble();
        qreal height = zone[JsonKeys::Height].toDouble();

        // Calculate new position with bounds checking
        qreal newZoneX = qBound(0.0, it.value().x() + dx, 1.0 - width);
        qreal newZoneY = qBound(0.0, it.value().y() + dy, 1.0 - height);

        // Update the zone's visual position directly (without creating undo commands)
        m_zoneManager->updateZoneGeometryDirect(it.key(), newZoneX, newZoneY, width, height);
    }

    m_zoneManager->endBatchUpdate();
}

void EditorController::endMultiZoneDrag(bool commit)
{
    if (!m_multiZoneDragActive || !m_zoneManager) {
        m_multiZoneDragActive = false;
        m_dragInitialPositions.clear();
        return;
    }

    if (commit && !m_dragInitialPositions.isEmpty()) {
        // Calculate final delta from primary zone
        QVariantMap primaryZone = m_zoneManager->getZoneById(m_dragPrimaryZoneId);
        if (!primaryZone.isEmpty() && m_dragInitialPositions.contains(m_dragPrimaryZoneId)) {
            qreal finalX = primaryZone[JsonKeys::X].toDouble();
            qreal finalY = primaryZone[JsonKeys::Y].toDouble();
            qreal dx = finalX - m_dragInitialPositions[m_dragPrimaryZoneId].x();
            qreal dy = finalY - m_dragInitialPositions[m_dragPrimaryZoneId].y();

            // Only create undo commands for other zones (primary zone already has its own)
            if (m_undoController && (qAbs(dx) > 0.0001 || qAbs(dy) > 0.0001)) {
                m_undoController->beginMacro(i18nc("@action", "Move %1 Zones", m_dragInitialPositions.count()));

                for (auto it = m_dragInitialPositions.constBegin(); it != m_dragInitialPositions.constEnd(); ++it) {
                    if (it.key() == m_dragPrimaryZoneId) {
                        continue; // Skip primary - it already has undo from normal updateZoneGeometry
                    }

                    QVariantMap zone = m_zoneManager->getZoneById(it.key());
                    if (zone.isEmpty()) {
                        continue;
                    }

                    qreal width = zone[JsonKeys::Width].toDouble();
                    qreal height = zone[JsonKeys::Height].toDouble();
                    qreal newX = qBound(0.0, it.value().x() + dx, 1.0 - width);
                    qreal newY = qBound(0.0, it.value().y() + dy, 1.0 - height);

                    // Create undo command for this zone
                    updateZoneGeometry(it.key(), newX, newY, width, height);
                }

                m_undoController->endMacro();
            }
        }
    } else if (!commit) {
        // Cancel - restore original positions
        m_zoneManager->beginBatchUpdate();
        for (auto it = m_dragInitialPositions.constBegin(); it != m_dragInitialPositions.constEnd(); ++it) {
            if (it.key() == m_dragPrimaryZoneId) {
                continue; // Primary zone's restore is handled by its drag handler
            }

            QVariantMap zone = m_zoneManager->getZoneById(it.key());
            if (!zone.isEmpty()) {
                qreal width = zone[JsonKeys::Width].toDouble();
                qreal height = zone[JsonKeys::Height].toDouble();
                m_zoneManager->updateZoneGeometryDirect(it.key(), it.value().x(), it.value().y(), width, height);
            }
        }
        m_zoneManager->endBatchUpdate();
    }

    m_multiZoneDragActive = false;
    m_dragPrimaryZoneId.clear();
    m_dragInitialPositions.clear();
}

bool EditorController::isMultiZoneDragActive() const
{
    return m_multiZoneDragActive;
}

void EditorController::updateSelectedZonesAppearance(const QString& propertyName, const QVariant& value)
{
    if (m_selectedZoneIds.isEmpty() || !m_undoController || !m_zoneManager) {
        return;
    }

    // For single selection, use existing implementation
    if (m_selectedZoneIds.count() == 1) {
        updateZoneAppearance(m_selectedZoneIds.first(), propertyName, value);
        return;
    }

    // Collect old values for undo
    QMap<QString, QVariant> oldValues;
    for (const QString& zoneId : m_selectedZoneIds) {
        QVariantMap zone = m_zoneManager->getZoneById(zoneId);
        if (!zone.isEmpty()) {
            oldValues[zoneId] = zone.value(propertyName);
        }
    }

    // Use batch command for single undo step with deferred signals
    auto* command = new BatchUpdateAppearanceCommand(QPointer<ZoneManager>(m_zoneManager), m_selectedZoneIds,
                                                     propertyName, oldValues, value);
    m_undoController->push(command);
    markUnsaved();
}

void EditorController::updateSelectedZonesColor(const QString& colorType, const QString& color)
{
    if (m_selectedZoneIds.isEmpty() || !m_undoController || !m_zoneManager) {
        return;
    }

    // For single selection, use existing implementation
    if (m_selectedZoneIds.count() == 1) {
        updateZoneColor(m_selectedZoneIds.first(), colorType, color);
        return;
    }

    // Collect old colors for undo
    QMap<QString, QString> oldColors;
    for (const QString& zoneId : m_selectedZoneIds) {
        QVariantMap zone = m_zoneManager->getZoneById(zoneId);
        if (!zone.isEmpty()) {
            oldColors[zoneId] = zone.value(colorType).toString();
        }
    }

    // Use batch command for single undo step with deferred signals
    auto* command = new BatchUpdateColorCommand(QPointer<ZoneManager>(m_zoneManager), m_selectedZoneIds, colorType,
                                                oldColors, color);
    m_undoController->push(command);
    markUnsaved();
}

/**
 * @brief Validates a zone name
 * @param zoneId The zone ID (to exclude from duplicate check)
 * @param name The name to validate
 * @return Empty string if valid, error message otherwise
 */
QString EditorController::validateZoneName(const QString& zoneId, const QString& name)
{
    // Empty names are allowed
    if (name.isEmpty()) {
        return QString();
    }

    // Check maximum length
    if (name.length() > 100) {
        return i18n("Zone name cannot exceed 100 characters");
    }

    // Check for invalid characters (allow alphanumeric, spaces, hyphens, underscores)
    // But be lenient - allow most characters for internationalization
    // Only block characters that could break JSON or filenames
    QRegularExpression invalidChars(QStringLiteral("[<>\"'\\\\]"));
    QRegularExpressionMatch match = invalidChars.match(name);
    if (match.hasMatch()) {
        return i18n("Zone name contains invalid characters: < > \" ' \\");
    }

    // Check for duplicate names (excluding the current zone)
    if (m_zoneManager) {
        QVariantList zones = m_zoneManager->zones();
        for (const QVariant& zoneVar : zones) {
            QVariantMap zone = zoneVar.toMap();
            QString otherZoneId = zone.value(QStringLiteral("id")).toString();
            if (otherZoneId != zoneId) {
                QString otherName = zone.value(QStringLiteral("name")).toString();
                if (otherName == name) {
                    return i18n("A zone with this name already exists");
                }
            }
        }
    }

    return QString(); // Valid
}

/**
 * @brief Validates a zone number
 * @param zoneId The zone ID (to exclude from duplicate check)
 * @param number The number to validate
 * @return Empty string if valid, error message otherwise
 */
QString EditorController::validateZoneNumber(const QString& zoneId, int number)
{
    // Check range
    if (number < 1) {
        return i18n("Zone number must be at least 1");
    }
    if (number > 99) {
        return i18n("Zone number cannot exceed 99");
    }

    // Check for duplicate numbers
    if (!m_zoneManager) {
        return QString(); // Can't check duplicates without manager
    }

    QVariantList zones = m_zoneManager->zones();
    for (const QVariant& zoneVar : zones) {
        QVariantMap zone = zoneVar.toMap();
        QString otherZoneId = zone[JsonKeys::Id].toString();

        // Skip the zone being updated
        if (otherZoneId == zoneId) {
            continue;
        }

        int otherNumber = zone[JsonKeys::ZoneNumber].toInt();
        if (otherNumber == number) {
            return i18n("Zone number %1 is already in use", number);
        }
    }

    return QString(); // Valid
}

void EditorController::setDefaultZoneColors(const QString& highlightColor, const QString& inactiveColor,
                                            const QString& borderColor)
{
    // Store defaults for use in template application
    m_defaultHighlightColor = highlightColor;
    m_defaultInactiveColor = inactiveColor;
    m_defaultBorderColor = borderColor;

    // Set in ZoneManager for new zone creation
    if (m_zoneManager) {
        m_zoneManager->setDefaultColors(highlightColor, inactiveColor, borderColor);
    }
}

void EditorController::loadEditorSettings()
{
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
    KConfigGroup editorGroup = config->group(QStringLiteral("Editor"));

    // Note: Per-layout zonePadding/outerGap overrides are loaded from the layout JSON
    // in loadLayout(). The global settings are cached here for performance (avoids D-Bus calls).
    refreshGlobalZonePadding();
    refreshGlobalOuterGap();

    // Load label font settings from global Appearance config (read-only in editor)
    KConfigGroup appearanceGroup = config->group(QStringLiteral("Appearance"));
    m_labelFontFamily = appearanceGroup.readEntry(QLatin1String("LabelFontFamily"), QString());
    m_labelFontSizeScale = qBound(0.25, appearanceGroup.readEntry(QLatin1String("LabelFontSizeScale"), 1.0), 3.0);
    m_labelFontWeight = appearanceGroup.readEntry(QLatin1String("LabelFontWeight"), static_cast<int>(QFont::Bold));
    m_labelFontItalic = appearanceGroup.readEntry(QLatin1String("LabelFontItalic"), false);
    m_labelFontUnderline = appearanceGroup.readEntry(QLatin1String("LabelFontUnderline"), false);
    m_labelFontStrikeout = appearanceGroup.readEntry(QLatin1String("LabelFontStrikeout"), false);

    // Load snapping settings (backward compatible with single SnapInterval)
    bool gridEnabled = editorGroup.readEntry(QLatin1String("GridSnappingEnabled"), true);
    bool edgeEnabled = editorGroup.readEntry(QLatin1String("EdgeSnappingEnabled"), true);

    // Try to load separate X and Y intervals, fall back to single interval for backward compatibility
    qreal snapIntX = editorGroup.readEntry(QLatin1String("SnapIntervalX"), -1.0);
    qreal snapIntY = editorGroup.readEntry(QLatin1String("SnapIntervalY"), -1.0);
    qreal snapInt = editorGroup.readEntry(QLatin1String("SnapInterval"), EditorConstants::DefaultSnapInterval);

    // If separate intervals not found, use the single interval for both
    if (snapIntX < 0.0)
        snapIntX = snapInt;
    if (snapIntY < 0.0)
        snapIntY = snapInt;

    // Apply to snapping service
    m_snappingService->setGridSnappingEnabled(gridEnabled);
    m_snappingService->setEdgeSnappingEnabled(edgeEnabled);
    m_snappingService->setSnapIntervalX(snapIntX);
    m_snappingService->setSnapIntervalY(snapIntY);

    // Load app-specific keyboard shortcuts with validation
    // Note: Standard shortcuts (Save, Delete, Close) use Qt StandardKey (system shortcuts)
    loadShortcutSetting(editorGroup, QStringLiteral("EditorDuplicateShortcut"),
                        QStringLiteral("Ctrl+D"), m_editorDuplicateShortcut,
                        [this]() { Q_EMIT editorDuplicateShortcutChanged(); });

    loadShortcutSetting(editorGroup, QStringLiteral("EditorSplitHorizontalShortcut"),
                        QStringLiteral("Ctrl+Shift+H"), m_editorSplitHorizontalShortcut,
                        [this]() { Q_EMIT editorSplitHorizontalShortcutChanged(); });

    // Note: Default changed from Ctrl+Shift+V to Ctrl+Alt+V to avoid conflict with Paste with Offset
    loadShortcutSetting(editorGroup, QStringLiteral("EditorSplitVerticalShortcut"),
                        QStringLiteral("Ctrl+Alt+V"), m_editorSplitVerticalShortcut,
                        [this]() { Q_EMIT editorSplitVerticalShortcutChanged(); });

    loadShortcutSetting(editorGroup, QStringLiteral("EditorFillShortcut"),
                        QStringLiteral("Ctrl+Shift+F"), m_editorFillShortcut,
                        [this]() { Q_EMIT editorFillShortcutChanged(); });

    // Load snap override modifier
    int snapOverrideMod = editorGroup.readEntry(QLatin1String("SnapOverrideModifier"), 0x02000000);
    if (m_snapOverrideModifier != snapOverrideMod) {
        m_snapOverrideModifier = snapOverrideMod;
        Q_EMIT snapOverrideModifierChanged();
    }

    // Load fill-on-drop settings
    bool fillOnDropEn = editorGroup.readEntry(QLatin1String("FillOnDropEnabled"), true);
    if (m_fillOnDropEnabled != fillOnDropEn) {
        m_fillOnDropEnabled = fillOnDropEn;
        Q_EMIT fillOnDropEnabledChanged();
    }

    int fillOnDropMod = editorGroup.readEntry(QLatin1String("FillOnDropModifier"), 0x04000000); // Default: Ctrl
    if (m_fillOnDropModifier != fillOnDropMod) {
        m_fillOnDropModifier = fillOnDropMod;
        Q_EMIT fillOnDropModifierChanged();
    }
}

void EditorController::saveEditorSettings()
{
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
    KConfigGroup editorGroup = config->group(QStringLiteral("Editor"));

    // Save snapping settings (save both separate intervals and single for backward compatibility)
    editorGroup.writeEntry(QLatin1String("GridSnappingEnabled"), m_snappingService->gridSnappingEnabled());
    editorGroup.writeEntry(QLatin1String("EdgeSnappingEnabled"), m_snappingService->edgeSnappingEnabled());
    editorGroup.writeEntry(QLatin1String("SnapIntervalX"), m_snappingService->snapIntervalX());
    editorGroup.writeEntry(QLatin1String("SnapIntervalY"), m_snappingService->snapIntervalY());
    editorGroup.writeEntry(QLatin1String("SnapInterval"), m_snappingService->snapIntervalX()); // Backward compat

    // Save app-specific keyboard shortcuts
    // Note: Standard shortcuts (Save, Delete, Close) use Qt StandardKey (system shortcuts)
    editorGroup.writeEntry(QLatin1String("EditorDuplicateShortcut"), m_editorDuplicateShortcut);
    editorGroup.writeEntry(QLatin1String("EditorSplitHorizontalShortcut"), m_editorSplitHorizontalShortcut);
    editorGroup.writeEntry(QLatin1String("EditorSplitVerticalShortcut"), m_editorSplitVerticalShortcut);
    editorGroup.writeEntry(QLatin1String("EditorFillShortcut"), m_editorFillShortcut);

    // Save snap override modifier
    editorGroup.writeEntry(QLatin1String("SnapOverrideModifier"), m_snapOverrideModifier);

    // Save fill-on-drop settings
    editorGroup.writeEntry(QLatin1String("FillOnDropEnabled"), m_fillOnDropEnabled);
    editorGroup.writeEntry(QLatin1String("FillOnDropModifier"), m_fillOnDropModifier);

    config->sync();
}

int EditorController::zoneIndexById(const QString& zoneId) const
{
    if (!m_zoneManager) {
        return -1;
    }
    return m_zoneManager->findZoneIndex(zoneId);
}

QVariantMap EditorController::getZoneById(const QString& zoneId) const
{
    if (!m_zoneManager) {
        return QVariantMap();
    }
    return m_zoneManager->getZoneById(zoneId);
}

/**
 * @brief Finds zones that share an edge with the specified zone
 * @param zoneId The unique identifier of the zone
 * @param edgeX X coordinate of the edge to check (relative 0.0-1.0)
 * @param edgeY Y coordinate of the edge to check (relative 0.0-1.0)
 * @param threshold Distance threshold for edge detection (default 0.01)
 * @return QVariantList of zone information maps
 *
 * Used by divider system to find zones adjacent to a given edge.
 */
QVariantList EditorController::getZonesSharingEdge(const QString& zoneId, qreal edgeX, qreal edgeY, qreal threshold)
{
    if (!m_zoneManager) {
        qCWarning(lcEditor) << "ZoneManager not initialized";
        return QVariantList();
    }

    return m_zoneManager->getZonesSharingEdge(zoneId, edgeX, edgeY, threshold);
}

/**
 * @brief Splits a zone horizontally or vertically into two zones
 * @param zoneId The unique identifier of the zone to split
 * @param horizontal If true, split horizontally (top/bottom), otherwise vertically (left/right)
 * @return Zone ID of the newly created zone, or empty string on failure
 */
QString EditorController::splitZone(const QString& zoneId, bool horizontal)
{
    if (!servicesReady("split zone")) {
        return QString();
    }

    // Get original zone data before split
    QVariantMap originalZoneData = m_zoneManager->getZoneById(zoneId);
    if (originalZoneData.isEmpty()) {
        qCWarning(lcEditor) << "Zone not found for split:" << zoneId;
        return QString();
    }

    // Perform operation
    QString newZoneId = m_zoneManager->splitZone(zoneId, horizontal);
    if (newZoneId.isEmpty()) {
        return QString();
    }

    // Get new zones data (modified original + new zone)
    QVariantMap modifiedOriginalZone = m_zoneManager->getZoneById(zoneId);
    QVariantMap newZone = m_zoneManager->getZoneById(newZoneId);
    QVariantList newZonesData;
    newZonesData.append(modifiedOriginalZone);
    newZonesData.append(newZone);

    // Create and push command
    auto* command =
        new SplitZoneCommand(QPointer<ZoneManager>(m_zoneManager), zoneId, originalZoneData, newZonesData, QString());
    m_undoController->push(command);

    markUnsaved();
    return newZoneId;
}

/**
 * @brief Resizes zones at a divider position
 * @param zoneId1 Zone ID on one side of the divider
 * @param zoneId2 Zone ID on the other side of the divider
 * @param newDividerX New X position of divider (relative 0.0-1.0) for vertical dividers
 * @param newDividerY New Y position of divider (relative 0.0-1.0) for horizontal dividers
 * @param isVertical true for vertical divider, false for horizontal
 *
 * Resizes all zones on both sides of a divider to the new position.
 * Ensures zones maintain minimum size and don't overlap.
 * Emits zoneGeometryChanged for each affected zone.
 */
void EditorController::resizeZonesAtDivider(const QString& zoneId1, const QString& zoneId2, qreal newDividerX,
                                            qreal newDividerY, bool isVertical)
{
    if (!servicesReady("resize zones at divider")) {
        return;
    }

    auto oldGeometries = m_zoneManager->collectGeometriesAtDivider(zoneId1, zoneId2, isVertical);
    if (oldGeometries.isEmpty()) {
        qCWarning(lcEditor) << "No zones affected by divider resize";
        return;
    }

    auto* command = new DividerResizeCommand(QPointer<ZoneManager>(m_zoneManager), zoneId1, zoneId2, newDividerX,
                                             newDividerY, isVertical, std::move(oldGeometries), QString());
    m_undoController->push(command);
    markUnsaved();
}

/**
 * @brief Imports a layout from a JSON file
 * @param filePath Path to the JSON file to import
 *
 * Calls the D-Bus importLayout method and loads the imported layout into the editor.
 * Emits layoutLoadFailed if the import fails.
 */
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

bool EditorController::saveShaderPreset(const QString& filePath, const QString& shaderId,
                                        const QVariantMap& shaderParams, const QString& presetName)
{
    if (filePath.isEmpty()) {
        Q_EMIT shaderPresetSaveFailed(i18nc("@info", "File path cannot be empty"));
        return false;
    }

    if (ShaderRegistry::isNoneShader(shaderId)) {
        Q_EMIT shaderPresetSaveFailed(i18nc("@info", "No shader selected to save"));
        return false;
    }

    QString name = presetName;
    if (name.isEmpty()) {
        name = QFileInfo(filePath).completeBaseName();
    }

    QJsonObject obj;
    obj[QLatin1String(JsonKeys::Name)] = name;
    obj[QLatin1String(JsonKeys::ShaderId)] = shaderId;
    obj[QLatin1String(JsonKeys::ShaderParams)] = QJsonObject::fromVariantMap(shaderParams);

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QString error = i18nc("@info", "Failed to save preset: %1", file.errorString());
        Q_EMIT shaderPresetSaveFailed(error);
        qCWarning(lcEditor) << error;
        return false;
    }

    QByteArray json = QJsonDocument(obj).toJson(QJsonDocument::Indented);
    if (file.write(json) != json.size()) {
        QString error = i18nc("@info", "Failed to write preset file: %1", file.errorString());
        Q_EMIT shaderPresetSaveFailed(error);
        qCWarning(lcEditor) << error;
        return false;
    }

    return true;
}

QVariantMap EditorController::loadShaderPreset(const QString& filePath)
{
    QVariantMap result;

    if (filePath.isEmpty()) {
        Q_EMIT shaderPresetLoadFailed(i18nc("@info", "File path cannot be empty"));
        return result;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString error = i18nc("@info", "Failed to open preset file: %1", file.errorString());
        Q_EMIT shaderPresetLoadFailed(error);
        qCWarning(lcEditor) << error;
        return result;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        QString error = i18nc("@info", "Invalid preset file: %1", parseError.errorString());
        Q_EMIT shaderPresetLoadFailed(error);
        qCWarning(lcEditor) << error;
        return result;
    }

    if (!doc.isObject()) {
        QString error = i18nc("@info", "Preset file must contain a JSON object");
        Q_EMIT shaderPresetLoadFailed(error);
        qCWarning(lcEditor) << error;
        return result;
    }

    QJsonObject obj = doc.object();
    QString shaderId = obj[QLatin1String(JsonKeys::ShaderId)].toString();
    if (shaderId.isEmpty()) {
        QString error = i18nc("@info", "Preset file missing shader ID");
        Q_EMIT shaderPresetLoadFailed(error);
        qCWarning(lcEditor) << error;
        return result;
    }

    // Validate that shader exists in available shaders
    bool shaderFound = false;
    for (const QVariant& shaderVar : m_availableShaders) {
        QVariantMap shaderMap = shaderVar.toMap();
        if (shaderMap[QLatin1String("id")].toString() == shaderId) {
            shaderFound = true;
            break;
        }
    }
    if (!shaderFound) {
        QString error = i18nc("@info", "Shader in preset is no longer available");
        Q_EMIT shaderPresetLoadFailed(error);
        qCWarning(lcEditor) << error;
        return result;
    }

    QVariantMap shaderParams;
    if (obj.contains(QLatin1String(JsonKeys::ShaderParams))) {
        shaderParams = obj[QLatin1String(JsonKeys::ShaderParams)].toObject().toVariantMap();
    }

    result[QLatin1String(JsonKeys::Name)] = obj[QLatin1String(JsonKeys::Name)].toString();
    result[QLatin1String(JsonKeys::ShaderId)] = shaderId;
    result[QLatin1String(JsonKeys::ShaderParams)] = shaderParams;

    return result;
}

QString EditorController::shaderPresetDirectory()
{
    QString path = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/plasmazones/shader-presets");

    QDir dir(path);
    if (!dir.exists()) {
        if (dir.mkpath(QStringLiteral("."))) {
            qCDebug(lcEditor) << "Created shader preset directory:" << dir.absolutePath();
        } else {
            qCWarning(lcEditor) << "Failed to create shader preset directory:" << path;
        }
    }

    return path;
}

void EditorController::onClipboardChanged()
{
    bool newCanPaste = canPaste();
    if (m_canPaste != newCanPaste) {
        m_canPaste = newCanPaste;
        Q_EMIT canPasteChanged();
    }
}

void EditorController::copyZones(const QStringList& zoneIds)
{
    if (!m_zoneManager) {
        qCWarning(lcEditor) << "ZoneManager not initialized";
        Q_EMIT clipboardOperationFailed(i18nc("@info", "Zone manager not initialized"));
        return;
    }

    if (zoneIds.isEmpty()) {
        qCWarning(lcEditor) << "Empty zone ID list for copy";
        return;
    }

    // Collect zones to copy
    QVariantList zonesToCopy;
    QVariantList allZones = m_zoneManager->zones();

    for (const QVariant& zoneVar : allZones) {
        QVariantMap zone = zoneVar.toMap();
        QString zoneId = zone[JsonKeys::Id].toString();
        if (zoneIds.contains(zoneId)) {
            zonesToCopy.append(zone);
        }
    }

    if (zonesToCopy.isEmpty()) {
        qCWarning(lcEditor) << "No valid zones found to copy";
        return;
    }

    // Serialize to JSON using helper
    QString jsonData = ZoneSerialization::serializeZonesToClipboard(zonesToCopy);

    // Copy to clipboard
    QClipboard* clipboard = QGuiApplication::clipboard();

    // QClipboard::setMimeData() takes ownership of QMimeData
    // No need to specify parent - ownership is transferred to clipboard
    QMimeData* mimeData = new QMimeData();
    mimeData->setData(QStringLiteral("application/vnd.plasmazones.zones+json"), jsonData.toUtf8());
    mimeData->setData(QStringLiteral("application/json"), jsonData.toUtf8());
    mimeData->setText(jsonData); // Text fallback for debugging

    // Check if clipboard state will change (we're setting valid zone data, so canPaste will be true after)
    bool wasCanPaste = canPaste();
    clipboard->setMimeData(mimeData, QClipboard::Clipboard);

    // Emit signal if clipboard state changed (we just set valid data, so canPaste is now true)
    if (!wasCanPaste) {
        m_canPaste = true;
        Q_EMIT canPasteChanged();
    }
}

void EditorController::cutZones(const QStringList& zoneIds)
{
    if (zoneIds.isEmpty() || !m_undoController) {
        return;
    }

    // Copy first
    copyZones(zoneIds);

    // Then delete with undo macro for single undo step
    {
        BatchOperationScope scope(m_undoController, m_zoneManager,
                                  i18nc("@action", "Cut %1 Zones", zoneIds.count()));
        for (const QString& zoneId : zoneIds) {
            deleteZone(zoneId);
        }
    }
}

QStringList EditorController::pasteZones(bool withOffset)
{
    if (!m_undoController || !m_zoneManager) {
        qCWarning(lcEditor) << "Cannot paste zones - undo controller or zone manager is null";
        Q_EMIT clipboardOperationFailed(i18nc("@info", "Zone manager not initialized"));
        return QStringList();
    }

    // Get clipboard data
    QClipboard* clipboard = QGuiApplication::clipboard();
    QString clipboardText = clipboard->text();

    if (clipboardText.isEmpty()) {
        return QStringList();
    }

    // Deserialize zones using helper
    QVariantList zonesToPaste = ZoneSerialization::deserializeZonesFromClipboard(clipboardText);
    if (zonesToPaste.isEmpty()) {
        return QStringList();
    }

    // Calculate offset if needed
    qreal offsetX = 0.0;
    qreal offsetY = 0.0;
    if (withOffset) {
        offsetX = EditorConstants::DuplicateOffset;
        offsetY = EditorConstants::DuplicateOffset;
    }

    // Prepare zones with new IDs and adjusted positions
    QStringList newZoneIds;
    QVariantList preparedZones;
    int newZoneNumber = m_zoneManager->zoneCount() + 1;

    for (QVariant& zoneVar : zonesToPaste) {
        QVariantMap zone = zoneVar.toMap();

        // Generate new ID
        QString newId = QUuid::createUuid().toString();
        zone[JsonKeys::Id] = newId;

        // Adjust position if offset requested
        qreal x = zone[JsonKeys::X].toDouble() + offsetX;
        qreal y = zone[JsonKeys::Y].toDouble() + offsetY;
        qreal width = zone[JsonKeys::Width].toDouble();
        qreal height = zone[JsonKeys::Height].toDouble();

        // Clamp to bounds
        x = qBound(0.0, x, 1.0 - width);
        y = qBound(0.0, y, 1.0 - height);

        zone[JsonKeys::X] = x;
        zone[JsonKeys::Y] = y;
        zone[JsonKeys::ZoneNumber] = newZoneNumber++;

        newZoneIds.append(newId);
        preparedZones.append(zone);
    }

    // Use batch update to defer signals until all zones are added
    m_zoneManager->beginBatchUpdate();

    for (const QVariant& zoneVar : preparedZones) {
        QVariantMap zone = zoneVar.toMap();
        m_zoneManager->addZoneFromMap(zone);
    }

    m_zoneManager->endBatchUpdate();

    // Create and push single command for all pasted zones (handles atomic undo/redo)
    auto* command = new PasteZonesCommand(QPointer<ZoneManager>(m_zoneManager), preparedZones,
                                          i18nc("@action", "Paste %1 Zones", preparedZones.count()));
    m_undoController->push(command);

    // Select all pasted zones
    if (!newZoneIds.isEmpty()) {
        setSelectedZoneIds(newZoneIds);
        markUnsaved();
    }

    return newZoneIds;
}

// ═══════════════════════════════════════════════════════════════════════════════
// SHADER SUPPORT
// ═══════════════════════════════════════════════════════════════════════════════

QString EditorController::currentShaderId() const
{
    return m_currentShaderId;
}

QVariantMap EditorController::currentShaderParams() const
{
    return m_currentShaderParams;
}

QVariantList EditorController::currentShaderParameters() const
{
    // Return cached parameters - populated when shader is selected
    // This avoids D-Bus calls on every QML property access
    return m_cachedShaderParameters;
}

QString EditorController::noneShaderUuid() const
{
    return ShaderRegistry::noneShaderUuid();
}

void EditorController::setCurrentShaderId(const QString& id)
{
    // Validate: must be empty (no shader) or exist in available shaders
    bool isValid = id.isEmpty();
    if (!isValid) {
        for (const QVariant& shader : m_availableShaders) {
            if (shader.toMap().value(QLatin1String("id")).toString() == id) {
                isValid = true;
                break;
            }
        }
    }

    if (!isValid) {
        qCWarning(lcEditor) << "Invalid shader ID:" << id;
        return;
    }

    if (m_currentShaderId != id) {
        auto* cmd = new UpdateShaderIdCommand(this, m_currentShaderId, id);
        m_undoController->push(cmd);
    }
}

void EditorController::setCurrentShaderIdDirect(const QString& id)
{
    if (m_currentShaderId != id) {
        m_currentShaderId = id;

        // Update cached shader parameters
        if (id.isEmpty()) {
            m_cachedShaderParameters.clear();
        } else {
            QVariantMap info = getShaderInfo(id);
            m_cachedShaderParameters = info.value(QLatin1String("parameters")).toList();
        }

        markUnsaved();
        Q_EMIT currentShaderIdChanged();
        Q_EMIT currentShaderParametersChanged();
    }
}

void EditorController::setCurrentShaderParams(const QVariantMap& params)
{
    if (m_currentShaderParams != params) {
        // Create undo command for batch params change
        auto* cmd = new UpdateShaderParamsCommand(this, m_currentShaderParams, params);
        m_undoController->push(cmd);
    }
}

void EditorController::setCurrentShaderParamsDirect(const QVariantMap& params)
{
    if (m_currentShaderParams != params) {
        m_currentShaderParams = params;
        markUnsaved();
        Q_EMIT currentShaderParamsChanged();
    }
}

void EditorController::setShaderParameter(const QString& key, const QVariant& value)
{
    if (m_currentShaderParams.value(key) != value) {
        // Create undo command for single param change (supports merging)
        QVariant oldValue = m_currentShaderParams.value(key);
        auto* cmd = new UpdateShaderParamsCommand(this, key, oldValue, value);
        m_undoController->push(cmd);
    }
}

void EditorController::setShaderParameterDirect(const QString& key, const QVariant& value)
{
    if (m_currentShaderParams.value(key) != value) {
        m_currentShaderParams[key] = value;
        markUnsaved();
        Q_EMIT currentShaderParamsChanged();
    }
}

void EditorController::resetShaderParameters()
{
    if (!m_currentShaderParams.isEmpty()) {
        // Create undo command for reset (batch change to empty)
        auto* cmd = new UpdateShaderParamsCommand(this, m_currentShaderParams, QVariantMap(),
                                                  i18nc("@action", "Reset Shader Parameters"));
        m_undoController->push(cmd);
    }
}

void EditorController::switchShader(const QString& id, const QVariantMap& params)
{
    if (m_currentShaderId == id && m_currentShaderParams == params) {
        return;
    }

    m_undoController->beginMacro(i18nc("@action", "Switch Shader Effect"));
    setCurrentShaderId(id);
    setCurrentShaderParams(params);
    m_undoController->endMacro();
}

QVariantMap EditorController::stripStaleShaderParams(const QVariantMap& params) const
{
    if (params.isEmpty() || m_cachedShaderParameters.isEmpty()) {
        return params;
    }

    // Build set of valid param IDs for the current shader
    QSet<QString> validIds;
    for (const QVariant& paramVar : m_cachedShaderParameters) {
        QVariantMap paramDef = paramVar.toMap();
        QString id = paramDef.value(QStringLiteral("id")).toString();
        if (!id.isEmpty()) {
            validIds.insert(id);
        }
    }

    if (validIds.isEmpty()) {
        return params;
    }

    // Keep only params that belong to the current shader
    QVariantMap result;
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        if (validIds.contains(it.key())) {
            result[it.key()] = it.value();
        }
    }
    return result;
}

void EditorController::refreshAvailableShaders()
{
    m_shadersEnabled = ShaderDbusQueries::queryShadersEnabled();
    m_availableShaders = ShaderDbusQueries::queryAvailableShaders();

    Q_EMIT availableShadersChanged();
    Q_EMIT shadersEnabledChanged();
}

QVariantMap EditorController::getShaderInfo(const QString& shaderId) const
{
    return ShaderDbusQueries::queryShaderInfo(shaderId);
}

} // namespace PlasmaZones
