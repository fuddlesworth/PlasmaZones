// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "kcm_plasmazones.h"
#include "assignmentmanager.h"
#include "daemoncontroller.h"
#include "layoutmanager.h"
#include <algorithm>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingReply>
#include <QDBusPendingCallWatcher>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QtGui/QtGui>
#include <QtQml/qqmlextensionplugin.h>
#include <KConfig>
#include <KConfigGroup>
#include <KGlobalAccel>
#include <KLocalizedString>
#include <KPluginFactory>
#include <KSharedConfig>
#include "../src/config/configdefaults.h"
#include "../src/config/settings.h"
#include "../src/config/updatechecker.h"
#include "../src/core/constants.h"
#include "../src/core/interfaces.h"
#include "../src/core/layout.h"
#include "../src/core/logging.h"
#include "../src/autotile/AlgorithmRegistry.h"
#include "../src/autotile/TilingAlgorithm.h"
#include "../src/autotile/TilingState.h"
#include "../src/core/modifierutils.h"
#include "../src/core/utils.h"
#include "version.h"

// Import static QML module for shared components
Q_IMPORT_QML_PLUGIN(org_plasmazones_commonPlugin)

K_PLUGIN_CLASS_WITH_JSON(PlasmaZones::KCMPlasmaZones, "kcm_plasmazones.json")

namespace PlasmaZones {

KCMPlasmaZones::KCMPlasmaZones(QObject* parent, const KPluginMetaData& data)
    : KQuickConfigModule(parent, data)
{
    m_settings = new Settings(this);

    // Layout management (CRUD, async loading, filtering, pending states)
    m_layoutManager = std::make_unique<LayoutManager>(this, m_settings, this);
    connect(m_layoutManager.get(), &LayoutManager::layoutsChanged, this, &KCMPlasmaZones::layoutsChanged);
    connect(m_layoutManager.get(), &LayoutManager::layoutToSelectChanged, this, &KCMPlasmaZones::layoutToSelectChanged);
    connect(m_layoutManager.get(), &LayoutManager::needsSave, this, [this]() { setNeedsSave(true); });

    // Assignment management (screen/desktop/activity, quick layout slots, app rules)
    m_assignmentManager = std::make_unique<AssignmentManager>(this, m_settings, this);
    connect(m_assignmentManager.get(), &AssignmentManager::screenAssignmentsChanged, this, &KCMPlasmaZones::screenAssignmentsChanged);
    connect(m_assignmentManager.get(), &AssignmentManager::tilingScreenAssignmentsChanged, this, &KCMPlasmaZones::tilingScreenAssignmentsChanged);
    connect(m_assignmentManager.get(), &AssignmentManager::tilingActivityAssignmentsChanged, this, &KCMPlasmaZones::tilingActivityAssignmentsChanged);
    connect(m_assignmentManager.get(), &AssignmentManager::tilingDesktopAssignmentsChanged, this, &KCMPlasmaZones::tilingDesktopAssignmentsChanged);
    connect(m_assignmentManager.get(), &AssignmentManager::assignmentViewModeChanged, this, &KCMPlasmaZones::assignmentViewModeChanged);
    connect(m_assignmentManager.get(), &AssignmentManager::quickLayoutSlotsChanged, this, &KCMPlasmaZones::quickLayoutSlotsChanged);
    connect(m_assignmentManager.get(), &AssignmentManager::tilingQuickLayoutSlotsChanged, this, &KCMPlasmaZones::tilingQuickLayoutSlotsChanged);
    connect(m_assignmentManager.get(), &AssignmentManager::activityAssignmentsChanged, this, &KCMPlasmaZones::activityAssignmentsChanged);
    connect(m_assignmentManager.get(), &AssignmentManager::appRulesRefreshed, this, &KCMPlasmaZones::appRulesRefreshed);
    connect(m_assignmentManager.get(), &AssignmentManager::needsSave, this, [this]() { setNeedsSave(true); });
    connect(m_assignmentManager.get(), &AssignmentManager::refreshScreensRequested, this, &KCMPlasmaZones::refreshScreens);

    m_layoutManager->loadSync();
    refreshScreens();

    // Daemon lifecycle management (status polling, D-Bus watcher, systemd control)
    m_daemonController = std::make_unique<DaemonController>(this, this);
    connect(m_daemonController.get(), &DaemonController::runningChanged, this, [this]() {
        Q_EMIT daemonRunningChanged();
        if (m_daemonController->isRunning()) {
            scheduleLoadLayouts();
            refreshScreens();
        }
    });
    connect(m_daemonController.get(), &DaemonController::enabledChanged,
            this, &KCMPlasmaZones::daemonEnabledChanged);

    // Set up update checker
    m_updateChecker = new UpdateChecker(this);
    connect(m_updateChecker, &UpdateChecker::updateAvailableChanged, this, &KCMPlasmaZones::updateAvailableChanged);
    connect(m_updateChecker, &UpdateChecker::latestVersionChanged, this, &KCMPlasmaZones::latestVersionChanged);
    connect(m_updateChecker, &UpdateChecker::releaseUrlChanged, this, &KCMPlasmaZones::releaseUrlChanged);
    connect(m_updateChecker, &UpdateChecker::checkingChanged, this, &KCMPlasmaZones::checkingForUpdatesChanged);

    // Load cached dismissed version
    KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
    KConfigGroup updatesGroup = config->group(QStringLiteral("Updates"));
    m_dismissedUpdateVersion = updatesGroup.readEntry("DismissedUpdateVersion", QString());

    // Check for updates when KCM loads
    m_updateChecker->checkForUpdates();

    // Debounce timer for coalescing rapid D-Bus layout signals into a single loadLayouts() call.
    // Without this, editing a layout (name, zones, etc.) can trigger multiple D-Bus signals
    // Listen for layout changes from the daemon — all routed through debounce timer
    QDBusConnection::sessionBus().connect(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                          QString(DBus::Interface::LayoutManager), QStringLiteral("layoutListChanged"),
                                          this, SLOT(scheduleLoadLayouts()));

    // Also listen for individual layout changes (when a specific layout is updated)
    QDBusConnection::sessionBus().connect(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                          QString(DBus::Interface::LayoutManager), QStringLiteral("layoutChanged"),
                                          this, SLOT(scheduleLoadLayouts()));

    // Listen for daemon ready signal (emitted when daemon finishes initialization)
    QDBusConnection::sessionBus().connect(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                          QString(DBus::Interface::LayoutManager), QStringLiteral("daemonReady"), this,
                                          SLOT(scheduleLoadLayouts()));

    // Listen for screen changes from the daemon
    QDBusConnection::sessionBus().connect(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                          QString(DBus::Interface::Screen), QStringLiteral("screenAdded"), this,
                                          SLOT(refreshScreens()));
    QDBusConnection::sessionBus().connect(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                          QString(DBus::Interface::Screen), QStringLiteral("screenRemoved"), this,
                                          SLOT(refreshScreens()));

    // Listen for screen layout assignment changes (routed to AssignmentManager)
    QDBusConnection::sessionBus().connect(
        QString(DBus::ServiceName), QString(DBus::ObjectPath), QString(DBus::Interface::LayoutManager),
        QStringLiteral("screenLayoutChanged"), m_assignmentManager.get(),
        SLOT(onScreenLayoutChanged(QString, QString)));

    // Listen for quick layout slot changes (routed to AssignmentManager)
    QDBusConnection::sessionBus().connect(
        QString(DBus::ServiceName), QString(DBus::ObjectPath), QString(DBus::Interface::LayoutManager),
        QStringLiteral("quickLayoutSlotsChanged"), m_assignmentManager.get(),
        SLOT(onQuickLayoutSlotsChanged()));

    // Listen for settings changes from daemon
    QDBusConnection::sessionBus().connect(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                          QString(DBus::Interface::Settings), QStringLiteral("settingsChanged"), this,
                                          SLOT(onSettingsChanged()));

    // Listen for virtual desktop count changes
    QDBusConnection::sessionBus().connect(
        QString(DBus::ServiceName), QString(DBus::ObjectPath), QString(DBus::Interface::LayoutManager),
        QStringLiteral("virtualDesktopCountChanged"), this, SLOT(refreshVirtualDesktops()));

    // Listen for KDE Activities changes
    QDBusConnection::sessionBus().connect(
        QString(DBus::ServiceName), QString(DBus::ObjectPath), QString(DBus::Interface::LayoutManager),
        QStringLiteral("currentActivityChanged"), this, SLOT(onCurrentActivityChanged(QString)));
    QDBusConnection::sessionBus().connect(
        QString(DBus::ServiceName), QString(DBus::ObjectPath), QString(DBus::Interface::LayoutManager),
        QStringLiteral("activitiesChanged"), this, SLOT(onActivitiesChanged()));

    // Initial virtual desktop refresh
    refreshVirtualDesktops();

    // Initial activities refresh
    refreshActivities();
}

KCMPlasmaZones::~KCMPlasmaZones()
{
}

KConfigGroup KCMPlasmaZones::editorConfigGroup()
{
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
    return config->group(QStringLiteral("Editor"));
}

// Activation getters
QVariantList KCMPlasmaZones::dragActivationTriggers() const
{
    return convertTriggersForQml(m_settings->dragActivationTriggers());
}
bool KCMPlasmaZones::alwaysActivateOnDrag() const
{
    const int alwaysActive = static_cast<int>(DragModifier::AlwaysActive);
    const auto triggers = m_settings->dragActivationTriggers();
    for (const auto& t : triggers) {
        if (t.toMap().value(QStringLiteral("modifier"), 0).toInt() == alwaysActive) {
            return true;
        }
    }
    return false;
}
bool KCMPlasmaZones::zoneSpanEnabled() const
{
    return m_settings->zoneSpanEnabled();
}
QVariantList KCMPlasmaZones::zoneSpanTriggers() const
{
    return convertTriggersForQml(m_settings->zoneSpanTriggers());
}

bool KCMPlasmaZones::toggleActivation() const
{
    return m_settings->toggleActivation();
}

bool KCMPlasmaZones::snappingEnabled() const
{
    return m_settings->snappingEnabled();
}

// Display getters
bool KCMPlasmaZones::showZonesOnAllMonitors() const
{
    return m_settings->showZonesOnAllMonitors();
}
QStringList KCMPlasmaZones::disabledMonitors() const
{
    return m_settings ? m_settings->disabledMonitors() : QStringList();
}
bool KCMPlasmaZones::showZoneNumbers() const
{
    return m_settings->showZoneNumbers();
}
bool KCMPlasmaZones::flashZonesOnSwitch() const
{
    return m_settings->flashZonesOnSwitch();
}
bool KCMPlasmaZones::showOsdOnLayoutSwitch() const
{
    return m_settings->showOsdOnLayoutSwitch();
}
bool KCMPlasmaZones::showNavigationOsd() const
{
    return m_settings->showNavigationOsd();
}
int KCMPlasmaZones::osdStyle() const
{
    return m_settings->osdStyleInt();
}

// Appearance getters
bool KCMPlasmaZones::useSystemColors() const
{
    return m_settings->useSystemColors();
}
QColor KCMPlasmaZones::highlightColor() const
{
    return m_settings->highlightColor();
}
QColor KCMPlasmaZones::inactiveColor() const
{
    return m_settings->inactiveColor();
}
QColor KCMPlasmaZones::borderColor() const
{
    return m_settings->borderColor();
}
QColor KCMPlasmaZones::labelFontColor() const
{
    return m_settings->labelFontColor();
}
qreal KCMPlasmaZones::activeOpacity() const
{
    return m_settings->activeOpacity();
}
qreal KCMPlasmaZones::inactiveOpacity() const
{
    return m_settings->inactiveOpacity();
}
int KCMPlasmaZones::borderWidth() const
{
    return m_settings->borderWidth();
}
int KCMPlasmaZones::borderRadius() const
{
    return m_settings->borderRadius();
}
bool KCMPlasmaZones::enableBlur() const
{
    return m_settings->enableBlur();
}
QString KCMPlasmaZones::labelFontFamily() const
{
    return m_settings->labelFontFamily();
}
qreal KCMPlasmaZones::labelFontSizeScale() const
{
    return m_settings->labelFontSizeScale();
}
int KCMPlasmaZones::labelFontWeight() const
{
    return m_settings->labelFontWeight();
}
bool KCMPlasmaZones::labelFontItalic() const
{
    return m_settings->labelFontItalic();
}
bool KCMPlasmaZones::labelFontUnderline() const
{
    return m_settings->labelFontUnderline();
}
bool KCMPlasmaZones::labelFontStrikeout() const
{
    return m_settings->labelFontStrikeout();
}
bool KCMPlasmaZones::enableShaderEffects() const
{
    return m_settings->enableShaderEffects();
}
int KCMPlasmaZones::shaderFrameRate() const
{
    return m_settings->shaderFrameRate();
}

// Zone getters
int KCMPlasmaZones::zonePadding() const
{
    return m_settings->zonePadding();
}
int KCMPlasmaZones::outerGap() const
{
    return m_settings->outerGap();
}
bool KCMPlasmaZones::usePerSideOuterGap() const
{
    return m_settings->usePerSideOuterGap();
}
int KCMPlasmaZones::outerGapTop() const
{
    return m_settings->outerGapTop();
}
int KCMPlasmaZones::outerGapBottom() const
{
    return m_settings->outerGapBottom();
}
int KCMPlasmaZones::outerGapLeft() const
{
    return m_settings->outerGapLeft();
}
int KCMPlasmaZones::outerGapRight() const
{
    return m_settings->outerGapRight();
}
int KCMPlasmaZones::adjacentThreshold() const
{
    return m_settings->adjacentThreshold();
}

// Behavior getters
bool KCMPlasmaZones::keepWindowsInZonesOnResolutionChange() const
{
    return m_settings->keepWindowsInZonesOnResolutionChange();
}
bool KCMPlasmaZones::moveNewWindowsToLastZone() const
{
    return m_settings->moveNewWindowsToLastZone();
}
bool KCMPlasmaZones::restoreOriginalSizeOnUnsnap() const
{
    return m_settings->restoreOriginalSizeOnUnsnap();
}
int KCMPlasmaZones::stickyWindowHandling() const
{
    return static_cast<int>(m_settings->stickyWindowHandling());
}
bool KCMPlasmaZones::restoreWindowsToZonesOnLogin() const
{
    return m_settings->restoreWindowsToZonesOnLogin();
}
bool KCMPlasmaZones::snapAssistFeatureEnabled() const
{
    return m_settings->snapAssistFeatureEnabled();
}
bool KCMPlasmaZones::snapAssistEnabled() const
{
    return m_settings->snapAssistEnabled();
}
QVariantList KCMPlasmaZones::snapAssistTriggers() const
{
    return convertTriggersForQml(m_settings->snapAssistTriggers());
}
QVariantList KCMPlasmaZones::defaultSnapAssistTriggers() const
{
    return convertTriggersForQml(ConfigDefaults::snapAssistTriggers());
}
QString KCMPlasmaZones::defaultLayoutId() const
{
    return m_settings->defaultLayoutId();
}

// Exclusions getters
QStringList KCMPlasmaZones::excludedApplications() const
{
    return m_settings->excludedApplications();
}
QStringList KCMPlasmaZones::excludedWindowClasses() const
{
    return m_settings->excludedWindowClasses();
}
bool KCMPlasmaZones::excludeTransientWindows() const
{
    return m_settings->excludeTransientWindows();
}
int KCMPlasmaZones::minimumWindowWidth() const
{
    return m_settings->minimumWindowWidth();
}
int KCMPlasmaZones::minimumWindowHeight() const
{
    return m_settings->minimumWindowHeight();
}

// Zone selector getters
bool KCMPlasmaZones::zoneSelectorEnabled() const
{
    return m_settings->zoneSelectorEnabled();
}
int KCMPlasmaZones::zoneSelectorTriggerDistance() const
{
    return m_settings->zoneSelectorTriggerDistance();
}
int KCMPlasmaZones::zoneSelectorPosition() const
{
    return m_settings->zoneSelectorPositionInt();
}
int KCMPlasmaZones::zoneSelectorLayoutMode() const
{
    return m_settings->zoneSelectorLayoutModeInt();
}
int KCMPlasmaZones::zoneSelectorPreviewWidth() const
{
    return m_settings->zoneSelectorPreviewWidth();
}
int KCMPlasmaZones::zoneSelectorPreviewHeight() const
{
    return m_settings->zoneSelectorPreviewHeight();
}
bool KCMPlasmaZones::zoneSelectorPreviewLockAspect() const
{
    return m_settings->zoneSelectorPreviewLockAspect();
}
int KCMPlasmaZones::zoneSelectorGridColumns() const
{
    return m_settings->zoneSelectorGridColumns();
}
int KCMPlasmaZones::zoneSelectorSizeMode() const
{
    return m_settings->zoneSelectorSizeModeInt();
}
int KCMPlasmaZones::zoneSelectorMaxRows() const
{
    return m_settings->zoneSelectorMaxRows();
}

// Editor shortcuts getters (read directly from KConfig Editor group)
// Note: Save, Delete, Close shortcuts now use Qt StandardKey (system shortcuts)
QString KCMPlasmaZones::editorDuplicateShortcut() const
{
    return editorConfigGroup().readEntry(QLatin1String("EditorDuplicateShortcut"), QStringLiteral("Ctrl+D"));
}

QString KCMPlasmaZones::editorSplitHorizontalShortcut() const
{
    return editorConfigGroup().readEntry(QLatin1String("EditorSplitHorizontalShortcut"), QStringLiteral("Ctrl+Shift+H"));
}

QString KCMPlasmaZones::editorSplitVerticalShortcut() const
{
    // Note: Default changed from Ctrl+Shift+V to Ctrl+Alt+V to avoid conflict with Paste with Offset
    return editorConfigGroup().readEntry(QLatin1String("EditorSplitVerticalShortcut"), QStringLiteral("Ctrl+Alt+V"));
}

QString KCMPlasmaZones::editorFillShortcut() const
{
    return editorConfigGroup().readEntry(QLatin1String("EditorFillShortcut"), QStringLiteral("Ctrl+Shift+F"));
}

// Editor snapping settings getters (read from KConfig Editor group)
bool KCMPlasmaZones::editorGridSnappingEnabled() const
{
    return editorConfigGroup().readEntry(QLatin1String("GridSnappingEnabled"), true);
}

bool KCMPlasmaZones::editorEdgeSnappingEnabled() const
{
    return editorConfigGroup().readEntry(QLatin1String("EdgeSnappingEnabled"), true);
}

qreal KCMPlasmaZones::editorSnapIntervalX() const
{
    KConfigGroup group = editorConfigGroup();
    qreal intervalX = group.readEntry(QLatin1String("SnapIntervalX"), -1.0);
    if (intervalX < 0.0) {
        // Fall back to single SnapInterval for backward compatibility
        intervalX = group.readEntry(QLatin1String("SnapInterval"), 0.1);
    }
    return intervalX;
}

qreal KCMPlasmaZones::editorSnapIntervalY() const
{
    KConfigGroup group = editorConfigGroup();
    qreal intervalY = group.readEntry(QLatin1String("SnapIntervalY"), -1.0);
    if (intervalY < 0.0) {
        // Fall back to single SnapInterval for backward compatibility
        intervalY = group.readEntry(QLatin1String("SnapInterval"), 0.1);
    }
    return intervalY;
}

int KCMPlasmaZones::editorSnapOverrideModifier() const
{
    return editorConfigGroup().readEntry(QLatin1String("SnapOverrideModifier"), 0x02000000); // Qt::ShiftModifier
}

// Fill on drop getters (read from KConfig Editor group)
bool KCMPlasmaZones::fillOnDropEnabled() const
{
    return editorConfigGroup().readEntry(QLatin1String("FillOnDropEnabled"), true);
}

int KCMPlasmaZones::fillOnDropModifier() const
{
    return editorConfigGroup().readEntry(QLatin1String("FillOnDropModifier"), 0x04000000); // Qt::ControlModifier
}

// Default value getters (for reset-to-default buttons in UI)
QVariantList KCMPlasmaZones::defaultDragActivationTriggers() const
{
    return convertTriggersForQml(ConfigDefaults::dragActivationTriggers());
}

QVariantList KCMPlasmaZones::defaultZoneSpanTriggers() const
{
    return convertTriggersForQml(ConfigDefaults::zoneSpanTriggers());
}

int KCMPlasmaZones::defaultEditorSnapOverrideModifier() const
{
    return 0x02000000; // Qt::ShiftModifier — matches readEntry default
}

int KCMPlasmaZones::defaultFillOnDropModifier() const
{
    return 0x04000000; // Qt::ControlModifier — matches readEntry default
}

QString KCMPlasmaZones::defaultEditorDuplicateShortcut() const
{
    return QStringLiteral("Ctrl+D");
}

QString KCMPlasmaZones::defaultEditorSplitHorizontalShortcut() const
{
    return QStringLiteral("Ctrl+Shift+H");
}

QString KCMPlasmaZones::defaultEditorSplitVerticalShortcut() const
{
    return QStringLiteral("Ctrl+Alt+V");
}

QString KCMPlasmaZones::defaultEditorFillShortcut() const
{
    return QStringLiteral("Ctrl+Shift+F");
}

// Layouts getter
QVariantList KCMPlasmaZones::layouts() const
{
    return m_layoutManager->layouts();
}

// Layout to select getter
QString KCMPlasmaZones::layoutToSelect() const
{
    return m_layoutManager->layoutToSelect();
}

// Screens and assignments getters
QVariantList KCMPlasmaZones::screens() const
{
    return m_screens;
}
QVariantMap KCMPlasmaZones::screenAssignments() const { return m_assignmentManager->screenAssignments(); }
int KCMPlasmaZones::assignmentViewMode() const { return m_assignmentManager->assignmentViewMode(); }
void KCMPlasmaZones::setAssignmentViewMode(int mode) { m_assignmentManager->setAssignmentViewMode(mode); }
QVariantMap KCMPlasmaZones::tilingScreenAssignments() const { return m_assignmentManager->tilingScreenAssignments(); }

// Virtual desktop getters
int KCMPlasmaZones::virtualDesktopCount() const
{
    return m_virtualDesktopCount;
}
QStringList KCMPlasmaZones::virtualDesktopNames() const
{
    return m_virtualDesktopNames;
}

// Activation setters
void KCMPlasmaZones::setDragActivationTriggers(const QVariantList& triggers)
{
    const bool wasAlwaysActive = alwaysActivateOnDrag();
    const QVariantList converted = convertTriggersForStorage(triggers);
    if (m_settings->dragActivationTriggers() != converted) {
        m_settings->setDragActivationTriggers(converted);
        Q_EMIT dragActivationTriggersChanged();
        if (alwaysActivateOnDrag() != wasAlwaysActive) {
            Q_EMIT alwaysActivateOnDragChanged();
        }
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setAlwaysActivateOnDrag(bool enabled)
{
    if (alwaysActivateOnDrag() == enabled) {
        return;
    }
    if (enabled) {
        // Single AlwaysActive trigger — written directly in storage format (DragModifier enum)
        QVariantMap trigger;
        trigger[QStringLiteral("modifier")] = static_cast<int>(DragModifier::AlwaysActive);
        trigger[QStringLiteral("mouseButton")] = 0;
        m_settings->setDragActivationTriggers({trigger});
    } else {
        // Revert to default triggers
        m_settings->setDragActivationTriggers(ConfigDefaults::dragActivationTriggers());
    }
    Q_EMIT alwaysActivateOnDragChanged();
    Q_EMIT dragActivationTriggersChanged();
    setNeedsSave(true);
}

void KCMPlasmaZones::setZoneSpanEnabled(bool enabled)
{
    if (m_settings->zoneSpanEnabled() != enabled) {
        m_settings->setZoneSpanEnabled(enabled);
        Q_EMIT zoneSpanEnabledChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setZoneSpanTriggers(const QVariantList& triggers)
{
    const QVariantList converted = convertTriggersForStorage(triggers);
    if (m_settings->zoneSpanTriggers() != converted) {
        m_settings->setZoneSpanTriggers(converted);
        Q_EMIT zoneSpanTriggersChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setToggleActivation(bool enable)
{
    if (m_settings->toggleActivation() != enable) {
        m_settings->setToggleActivation(enable);
        Q_EMIT toggleActivationChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setSnappingEnabled(bool enabled)
{
    if (m_settings->snappingEnabled() != enabled) {
        m_settings->setSnappingEnabled(enabled);
        Q_EMIT snappingEnabledChanged();
        setNeedsSave(true);
    }
}

// Display setters
void KCMPlasmaZones::setShowZonesOnAllMonitors(bool show)
{
    if (m_settings->showZonesOnAllMonitors() != show) {
        m_settings->setShowZonesOnAllMonitors(show);
        Q_EMIT showZonesOnAllMonitorsChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setShowZoneNumbers(bool show)
{
    if (m_settings->showZoneNumbers() != show) {
        m_settings->setShowZoneNumbers(show);
        Q_EMIT showZoneNumbersChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setFlashZonesOnSwitch(bool flash)
{
    if (m_settings->flashZonesOnSwitch() != flash) {
        m_settings->setFlashZonesOnSwitch(flash);
        Q_EMIT flashZonesOnSwitchChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setShowOsdOnLayoutSwitch(bool show)
{
    if (m_settings->showOsdOnLayoutSwitch() != show) {
        m_settings->setShowOsdOnLayoutSwitch(show);
        Q_EMIT showOsdOnLayoutSwitchChanged();
        setNeedsSave(true);
    }
}
void KCMPlasmaZones::setShowNavigationOsd(bool show)
{
    if (m_settings->showNavigationOsd() != show) {
        m_settings->setShowNavigationOsd(show);
        Q_EMIT showNavigationOsdChanged();
        setNeedsSave(true);
    }
}
void KCMPlasmaZones::setOsdStyle(int style)
{
    if (m_settings->osdStyleInt() != style) {
        m_settings->setOsdStyleInt(style);
        Q_EMIT osdStyleChanged();
        setNeedsSave(true);
    }
}

// Appearance setters
void KCMPlasmaZones::setUseSystemColors(bool use)
{
    if (m_settings->useSystemColors() != use) {
        m_settings->setUseSystemColors(use);
        Q_EMIT useSystemColorsChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setHighlightColor(const QColor& color)
{
    if (m_settings->highlightColor() != color) {
        m_settings->setHighlightColor(color);
        Q_EMIT highlightColorChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setInactiveColor(const QColor& color)
{
    if (m_settings->inactiveColor() != color) {
        m_settings->setInactiveColor(color);
        Q_EMIT inactiveColorChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setBorderColor(const QColor& color)
{
    if (m_settings->borderColor() != color) {
        m_settings->setBorderColor(color);
        Q_EMIT borderColorChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setLabelFontColor(const QColor& color)
{
    if (m_settings->labelFontColor() != color) {
        m_settings->setLabelFontColor(color);
        Q_EMIT labelFontColorChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setActiveOpacity(qreal opacity)
{
    if (!qFuzzyCompare(1.0 + m_settings->activeOpacity(), 1.0 + opacity)) {
        m_settings->setActiveOpacity(opacity);
        Q_EMIT activeOpacityChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setInactiveOpacity(qreal opacity)
{
    if (!qFuzzyCompare(1.0 + m_settings->inactiveOpacity(), 1.0 + opacity)) {
        m_settings->setInactiveOpacity(opacity);
        Q_EMIT inactiveOpacityChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setBorderWidth(int width)
{
    if (m_settings->borderWidth() != width) {
        m_settings->setBorderWidth(width);
        Q_EMIT borderWidthChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setBorderRadius(int radius)
{
    if (m_settings->borderRadius() != radius) {
        m_settings->setBorderRadius(radius);
        Q_EMIT borderRadiusChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setEnableBlur(bool enable)
{
    if (m_settings->enableBlur() != enable) {
        m_settings->setEnableBlur(enable);
        Q_EMIT enableBlurChanged();
        setNeedsSave(true);
    }
}
void KCMPlasmaZones::setLabelFontFamily(const QString& family)
{
    if (m_settings->labelFontFamily() != family) {
        m_settings->setLabelFontFamily(family);
        Q_EMIT labelFontFamilyChanged();
        setNeedsSave(true);
    }
}
void KCMPlasmaZones::setLabelFontSizeScale(qreal scale)
{
    scale = qBound(0.25, scale, 3.0);
    if (!qFuzzyCompare(m_settings->labelFontSizeScale(), scale)) {
        m_settings->setLabelFontSizeScale(scale);
        Q_EMIT labelFontSizeScaleChanged();
        setNeedsSave(true);
    }
}
void KCMPlasmaZones::setLabelFontWeight(int weight)
{
    if (m_settings->labelFontWeight() != weight) {
        m_settings->setLabelFontWeight(weight);
        Q_EMIT labelFontWeightChanged();
        setNeedsSave(true);
    }
}
void KCMPlasmaZones::setLabelFontItalic(bool italic)
{
    if (m_settings->labelFontItalic() != italic) {
        m_settings->setLabelFontItalic(italic);
        Q_EMIT labelFontItalicChanged();
        setNeedsSave(true);
    }
}
void KCMPlasmaZones::setLabelFontUnderline(bool underline)
{
    if (m_settings->labelFontUnderline() != underline) {
        m_settings->setLabelFontUnderline(underline);
        Q_EMIT labelFontUnderlineChanged();
        setNeedsSave(true);
    }
}
void KCMPlasmaZones::setLabelFontStrikeout(bool strikeout)
{
    if (m_settings->labelFontStrikeout() != strikeout) {
        m_settings->setLabelFontStrikeout(strikeout);
        Q_EMIT labelFontStrikeoutChanged();
        setNeedsSave(true);
    }
}
void KCMPlasmaZones::setEnableShaderEffects(bool enable)
{
    if (m_settings->enableShaderEffects() != enable) {
        m_settings->setEnableShaderEffects(enable);
        Q_EMIT enableShaderEffectsChanged();
        setNeedsSave(true);
    }
}
void KCMPlasmaZones::setShaderFrameRate(int fps)
{
    if (m_settings->shaderFrameRate() != fps) {
        m_settings->setShaderFrameRate(fps);
        Q_EMIT shaderFrameRateChanged();
        setNeedsSave(true);
    }
}

bool KCMPlasmaZones::enableAudioVisualizer() const
{
    return m_settings->enableAudioVisualizer();
}

bool KCMPlasmaZones::cavaAvailable() const
{
    return !QStandardPaths::findExecutable(QStringLiteral("cava")).isEmpty();
}

int KCMPlasmaZones::audioSpectrumBarCount() const
{
    return m_settings->audioSpectrumBarCount();
}

void KCMPlasmaZones::setEnableAudioVisualizer(bool enable)
{
    if (m_settings->enableAudioVisualizer() != enable) {
        m_settings->setEnableAudioVisualizer(enable);
        Q_EMIT enableAudioVisualizerChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setAudioSpectrumBarCount(int count)
{
    // CAVA requires even bar count for stereo output
    const int even = (count % 2 != 0) ? count + 1 : count;
    const int clamped = qBound(Audio::MinBars, even, Audio::MaxBars);
    if (m_settings->audioSpectrumBarCount() != clamped) {
        m_settings->setAudioSpectrumBarCount(clamped);
        Q_EMIT audioSpectrumBarCountChanged();
        setNeedsSave(true);
    }
}

// Zone setters
void KCMPlasmaZones::setZonePadding(int padding)
{
    if (m_settings->zonePadding() != padding) {
        m_settings->setZonePadding(padding);
        Q_EMIT zonePaddingChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setOuterGap(int gap)
{
    if (m_settings->outerGap() != gap) {
        m_settings->setOuterGap(gap);
        Q_EMIT outerGapChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setUsePerSideOuterGap(bool enabled)
{
    if (m_settings->usePerSideOuterGap() != enabled) {
        m_settings->setUsePerSideOuterGap(enabled);
        Q_EMIT usePerSideOuterGapChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setOuterGapTop(int gap)
{
    if (m_settings->outerGapTop() != gap) {
        m_settings->setOuterGapTop(gap);
        Q_EMIT outerGapTopChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setOuterGapBottom(int gap)
{
    if (m_settings->outerGapBottom() != gap) {
        m_settings->setOuterGapBottom(gap);
        Q_EMIT outerGapBottomChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setOuterGapLeft(int gap)
{
    if (m_settings->outerGapLeft() != gap) {
        m_settings->setOuterGapLeft(gap);
        Q_EMIT outerGapLeftChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setOuterGapRight(int gap)
{
    if (m_settings->outerGapRight() != gap) {
        m_settings->setOuterGapRight(gap);
        Q_EMIT outerGapRightChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setAdjacentThreshold(int threshold)
{
    if (m_settings->adjacentThreshold() != threshold) {
        m_settings->setAdjacentThreshold(threshold);
        Q_EMIT adjacentThresholdChanged();
        setNeedsSave(true);
    }
}

// Behavior setters
void KCMPlasmaZones::setKeepWindowsInZonesOnResolutionChange(bool keep)
{
    if (m_settings->keepWindowsInZonesOnResolutionChange() != keep) {
        m_settings->setKeepWindowsInZonesOnResolutionChange(keep);
        Q_EMIT keepWindowsInZonesOnResolutionChangeChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setMoveNewWindowsToLastZone(bool move)
{
    if (m_settings->moveNewWindowsToLastZone() != move) {
        m_settings->setMoveNewWindowsToLastZone(move);
        Q_EMIT moveNewWindowsToLastZoneChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setRestoreOriginalSizeOnUnsnap(bool restore)
{
    if (m_settings->restoreOriginalSizeOnUnsnap() != restore) {
        m_settings->setRestoreOriginalSizeOnUnsnap(restore);
        Q_EMIT restoreOriginalSizeOnUnsnapChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setStickyWindowHandling(int handling)
{
    int clamped = qBound(static_cast<int>(StickyWindowHandling::TreatAsNormal), handling,
                         static_cast<int>(StickyWindowHandling::IgnoreAll));
    if (static_cast<int>(m_settings->stickyWindowHandling()) != clamped) {
        m_settings->setStickyWindowHandling(static_cast<StickyWindowHandling>(clamped));
        Q_EMIT stickyWindowHandlingChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setRestoreWindowsToZonesOnLogin(bool restore)
{
    if (m_settings->restoreWindowsToZonesOnLogin() != restore) {
        m_settings->setRestoreWindowsToZonesOnLogin(restore);
        Q_EMIT restoreWindowsToZonesOnLoginChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setSnapAssistFeatureEnabled(bool enabled)
{
    if (m_settings->snapAssistFeatureEnabled() != enabled) {
        m_settings->setSnapAssistFeatureEnabled(enabled);
        Q_EMIT snapAssistFeatureEnabledChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setSnapAssistEnabled(bool enabled)
{
    if (m_settings->snapAssistEnabled() != enabled) {
        m_settings->setSnapAssistEnabled(enabled);
        Q_EMIT snapAssistEnabledChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setSnapAssistTriggers(const QVariantList& triggers)
{
    QVariantList converted = convertTriggersForStorage(triggers);
    if (m_settings->snapAssistTriggers() != converted) {
        m_settings->setSnapAssistTriggers(converted);
        Q_EMIT snapAssistTriggersChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setDefaultLayoutId(const QString& layoutId)
{
    if (m_settings->defaultLayoutId() != layoutId) {
        m_settings->setDefaultLayoutId(layoutId);
        Q_EMIT defaultLayoutIdChanged();
        setNeedsSave(true);
    }
}

// Exclusions setters
void KCMPlasmaZones::setExcludedApplications(const QStringList& apps)
{
    if (m_settings->excludedApplications() != apps) {
        m_settings->setExcludedApplications(apps);
        Q_EMIT excludedApplicationsChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setExcludedWindowClasses(const QStringList& classes)
{
    if (m_settings->excludedWindowClasses() != classes) {
        m_settings->setExcludedWindowClasses(classes);
        Q_EMIT excludedWindowClassesChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setExcludeTransientWindows(bool exclude)
{
    if (m_settings->excludeTransientWindows() != exclude) {
        m_settings->setExcludeTransientWindows(exclude);
        Q_EMIT excludeTransientWindowsChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setMinimumWindowWidth(int width)
{
    if (m_settings->minimumWindowWidth() != width) {
        m_settings->setMinimumWindowWidth(width);
        Q_EMIT minimumWindowWidthChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setMinimumWindowHeight(int height)
{
    if (m_settings->minimumWindowHeight() != height) {
        m_settings->setMinimumWindowHeight(height);
        Q_EMIT minimumWindowHeightChanged();
        setNeedsSave(true);
    }
}

// Zone selector setters
void KCMPlasmaZones::setZoneSelectorEnabled(bool enabled)
{
    if (m_settings->zoneSelectorEnabled() != enabled) {
        m_settings->setZoneSelectorEnabled(enabled);
        Q_EMIT zoneSelectorEnabledChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setZoneSelectorTriggerDistance(int distance)
{
    if (m_settings->zoneSelectorTriggerDistance() != distance) {
        m_settings->setZoneSelectorTriggerDistance(distance);
        Q_EMIT zoneSelectorTriggerDistanceChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setZoneSelectorPosition(int position)
{
    if (m_settings->zoneSelectorPositionInt() != position) {
        m_settings->setZoneSelectorPositionInt(position);
        Q_EMIT zoneSelectorPositionChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setZoneSelectorLayoutMode(int mode)
{
    if (m_settings->zoneSelectorLayoutModeInt() != mode) {
        m_settings->setZoneSelectorLayoutModeInt(mode);
        Q_EMIT zoneSelectorLayoutModeChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setZoneSelectorPreviewWidth(int width)
{
    if (m_settings->zoneSelectorPreviewWidth() != width) {
        m_settings->setZoneSelectorPreviewWidth(width);
        Q_EMIT zoneSelectorPreviewWidthChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setZoneSelectorPreviewHeight(int height)
{
    if (m_settings->zoneSelectorPreviewHeight() != height) {
        m_settings->setZoneSelectorPreviewHeight(height);
        Q_EMIT zoneSelectorPreviewHeightChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setZoneSelectorPreviewLockAspect(bool locked)
{
    if (m_settings->zoneSelectorPreviewLockAspect() != locked) {
        m_settings->setZoneSelectorPreviewLockAspect(locked);
        Q_EMIT zoneSelectorPreviewLockAspectChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setZoneSelectorGridColumns(int columns)
{
    if (m_settings->zoneSelectorGridColumns() != columns) {
        m_settings->setZoneSelectorGridColumns(columns);
        Q_EMIT zoneSelectorGridColumnsChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setZoneSelectorSizeMode(int mode)
{
    if (m_settings->zoneSelectorSizeModeInt() != mode) {
        m_settings->setZoneSelectorSizeModeInt(mode);
        Q_EMIT zoneSelectorSizeModeChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setZoneSelectorMaxRows(int rows)
{
    if (m_settings->zoneSelectorMaxRows() != rows) {
        m_settings->setZoneSelectorMaxRows(rows);
        Q_EMIT zoneSelectorMaxRowsChanged();
        setNeedsSave(true);
    }
}

// Autotiling getters
bool KCMPlasmaZones::autotileEnabled() const
{
    return m_settings->autotileEnabled();
}
QString KCMPlasmaZones::autotileAlgorithm() const
{
    return m_settings->autotileAlgorithm();
}
qreal KCMPlasmaZones::autotileSplitRatio() const
{
    return m_settings->autotileSplitRatio();
}
int KCMPlasmaZones::autotileMasterCount() const
{
    return m_settings->autotileMasterCount();
}
int KCMPlasmaZones::autotileInnerGap() const
{
    return m_settings->autotileInnerGap();
}
int KCMPlasmaZones::autotileOuterGap() const
{
    return m_settings->autotileOuterGap();
}
bool KCMPlasmaZones::autotileFocusNewWindows() const
{
    return m_settings->autotileFocusNewWindows();
}
bool KCMPlasmaZones::autotileSmartGaps() const
{
    return m_settings->autotileSmartGaps();
}
int KCMPlasmaZones::autotileMaxWindows() const
{
    return m_settings->autotileMaxWindows();
}
int KCMPlasmaZones::autotileInsertPosition() const
{
    return m_settings->autotileInsertPositionInt();
}
bool KCMPlasmaZones::animationsEnabled() const
{
    return m_settings->animationsEnabled();
}
int KCMPlasmaZones::animationDuration() const
{
    return m_settings->animationDuration();
}
QString KCMPlasmaZones::animationEasingCurve() const
{
    return m_settings->animationEasingCurve();
}
int KCMPlasmaZones::animationMinDistance() const
{
    return m_settings->animationMinDistance();
}

int KCMPlasmaZones::animationSequenceMode() const
{
    return m_settings->animationSequenceMode();
}

int KCMPlasmaZones::animationStaggerInterval() const
{
    return m_settings->animationStaggerInterval();
}

int KCMPlasmaZones::animationStaggerIntervalMax() const
{
    return static_cast<int>(AutotileDefaults::MaxAnimationStaggerIntervalMs);
}

bool KCMPlasmaZones::autotileFocusFollowsMouse() const
{
    return m_settings->autotileFocusFollowsMouse();
}
bool KCMPlasmaZones::autotileRespectMinimumSize() const
{
    return m_settings->autotileRespectMinimumSize();
}
bool KCMPlasmaZones::autotileHideTitleBars() const
{
    return m_settings->autotileHideTitleBars();
}
int KCMPlasmaZones::autotileBorderWidth() const
{
    return m_settings->autotileBorderWidth();
}
QColor KCMPlasmaZones::autotileBorderColor() const
{
    return m_settings->autotileBorderColor();
}
bool KCMPlasmaZones::autotileUseSystemBorderColors() const
{
    return m_settings->autotileUseSystemBorderColors();
}
bool KCMPlasmaZones::autotileUsePerSideOuterGap() const
{
    return m_settings->autotileUsePerSideOuterGap();
}
int KCMPlasmaZones::autotileOuterGapTop() const
{
    return m_settings->autotileOuterGapTop();
}
int KCMPlasmaZones::autotileOuterGapBottom() const
{
    return m_settings->autotileOuterGapBottom();
}
int KCMPlasmaZones::autotileOuterGapLeft() const
{
    return m_settings->autotileOuterGapLeft();
}
int KCMPlasmaZones::autotileOuterGapRight() const
{
    return m_settings->autotileOuterGapRight();
}

QVariantList KCMPlasmaZones::availableAlgorithms() const
{
    QVariantList algorithms;
    auto *registry = PlasmaZones::AlgorithmRegistry::instance();
    for (const QString &id : registry->availableAlgorithms()) {
        PlasmaZones::TilingAlgorithm *algo = registry->algorithm(id);
        if (algo) {
            QVariantMap algoMap;
            algoMap[QStringLiteral("id")] = id;
            algoMap[QStringLiteral("name")] = algo->name();
            algoMap[QStringLiteral("description")] = algo->description();
            algoMap[QStringLiteral("defaultMaxWindows")] = algo->defaultMaxWindows();
            algorithms.append(algoMap);
        }
    }
    return algorithms;
}

QVariantList KCMPlasmaZones::generateAlgorithmPreview(const QString &algorithmId, int windowCount,
                                                      double splitRatio, int masterCount) const
{
    auto *registry = PlasmaZones::AlgorithmRegistry::instance();
    PlasmaZones::TilingAlgorithm *algo = registry->algorithm(algorithmId);
    if (!algo) {
        return {};
    }

    const int previewSize = 1000;
    const QRect previewRect(0, 0, previewSize, previewSize);

    PlasmaZones::TilingState state(QStringLiteral("preview"));
    state.setMasterCount(masterCount);
    state.setSplitRatio(splitRatio);

    const int count = qMax(1, windowCount);
    QVector<QRect> zones = algo->calculateZones({count, previewRect, &state, 0, {}});

    return PlasmaZones::AlgorithmRegistry::zonesToRelativeGeometry(zones, previewRect);
}

// Autotiling setters
void KCMPlasmaZones::setAutotileEnabled(bool enabled)
{
    if (m_settings->autotileEnabled() != enabled) {
        m_settings->setAutotileEnabled(enabled);
        Q_EMIT autotileEnabledChanged();
        setNeedsSave(true);
        // Re-filter cached layouts to show/hide autotile entries without D-Bus re-fetch.
        // This avoids a synchronous D-Bus round-trip that blocks the UI thread and
        // causes scroll position resets in the Assignments tab.
        m_layoutManager->applyLayoutFilter();
    }
}

void KCMPlasmaZones::setAutotileAlgorithm(const QString& algorithm)
{
    if (m_settings->autotileAlgorithm() != algorithm) {
        m_settings->setAutotileAlgorithm(algorithm);
        Q_EMIT autotileAlgorithmChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setAutotileSplitRatio(qreal ratio)
{
    ratio = qBound(0.1, ratio, 0.9);
    if (!qFuzzyCompare(m_settings->autotileSplitRatio(), ratio)) {
        m_settings->setAutotileSplitRatio(ratio);
        Q_EMIT autotileSplitRatioChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setAutotileMasterCount(int count)
{
    count = qBound(1, count, 5);
    if (m_settings->autotileMasterCount() != count) {
        m_settings->setAutotileMasterCount(count);
        Q_EMIT autotileMasterCountChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setAutotileInnerGap(int gap)
{
    gap = qBound(0, gap, 50);
    if (m_settings->autotileInnerGap() != gap) {
        m_settings->setAutotileInnerGap(gap);
        Q_EMIT autotileInnerGapChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setAutotileOuterGap(int gap)
{
    gap = qBound(0, gap, 50);
    if (m_settings->autotileOuterGap() != gap) {
        m_settings->setAutotileOuterGap(gap);
        Q_EMIT autotileOuterGapChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setAutotileFocusNewWindows(bool focus)
{
    if (m_settings->autotileFocusNewWindows() != focus) {
        m_settings->setAutotileFocusNewWindows(focus);
        Q_EMIT autotileFocusNewWindowsChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setAutotileSmartGaps(bool smart)
{
    if (m_settings->autotileSmartGaps() != smart) {
        m_settings->setAutotileSmartGaps(smart);
        Q_EMIT autotileSmartGapsChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setAutotileMaxWindows(int count)
{
    count = qBound(1, count, 12);
    if (m_settings->autotileMaxWindows() != count) {
        m_settings->setAutotileMaxWindows(count);
        Q_EMIT autotileMaxWindowsChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setAutotileInsertPosition(int position)
{
    position = qBound(0, position, 2);
    if (m_settings->autotileInsertPositionInt() != position) {
        m_settings->setAutotileInsertPositionInt(position);
        Q_EMIT autotileInsertPositionChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setAnimationsEnabled(bool enabled)
{
    if (m_settings->animationsEnabled() != enabled) {
        m_settings->setAnimationsEnabled(enabled);
        Q_EMIT animationsEnabledChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setAnimationDuration(int duration)
{
    duration = qBound(50, duration, 500);
    if (m_settings->animationDuration() != duration) {
        m_settings->setAnimationDuration(duration);
        Q_EMIT animationDurationChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setAnimationEasingCurve(const QString& curve)
{
    if (m_settings->animationEasingCurve() != curve) {
        m_settings->setAnimationEasingCurve(curve);
        Q_EMIT animationEasingCurveChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setAnimationMinDistance(int distance)
{
    distance = qBound(0, distance, 200);
    if (m_settings->animationMinDistance() != distance) {
        m_settings->setAnimationMinDistance(distance);
        Q_EMIT animationMinDistanceChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setAnimationSequenceMode(int mode)
{
    mode = qBound(0, mode, 1);
    if (m_settings->animationSequenceMode() != mode) {
        m_settings->setAnimationSequenceMode(mode);
        Q_EMIT animationSequenceModeChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setAnimationStaggerInterval(int ms)
{
    ms = qBound(static_cast<int>(AutotileDefaults::MinAnimationStaggerIntervalMs),
                ms, static_cast<int>(AutotileDefaults::MaxAnimationStaggerIntervalMs));
    if (m_settings->animationStaggerInterval() != ms) {
        m_settings->setAnimationStaggerInterval(ms);
        Q_EMIT animationStaggerIntervalChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setAutotileFocusFollowsMouse(bool focus)
{
    if (m_settings->autotileFocusFollowsMouse() != focus) {
        m_settings->setAutotileFocusFollowsMouse(focus);
        Q_EMIT autotileFocusFollowsMouseChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setAutotileRespectMinimumSize(bool respect)
{
    if (m_settings->autotileRespectMinimumSize() != respect) {
        m_settings->setAutotileRespectMinimumSize(respect);
        Q_EMIT autotileRespectMinimumSizeChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setAutotileHideTitleBars(bool hide)
{
    if (m_settings->autotileHideTitleBars() != hide) {
        m_settings->setAutotileHideTitleBars(hide);
        Q_EMIT autotileHideTitleBarsChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setAutotileBorderWidth(int width)
{
    if (m_settings->autotileBorderWidth() != width) {
        m_settings->setAutotileBorderWidth(width);
        Q_EMIT autotileBorderWidthChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setAutotileBorderColor(const QColor& color)
{
    if (m_settings->autotileBorderColor() != color) {
        m_settings->setAutotileBorderColor(color);
        Q_EMIT autotileBorderColorChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setAutotileUseSystemBorderColors(bool use)
{
    if (m_settings->autotileUseSystemBorderColors() != use) {
        m_settings->setAutotileUseSystemBorderColors(use);
        Q_EMIT autotileUseSystemBorderColorsChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setAutotileUsePerSideOuterGap(bool enabled)
{
    if (m_settings->autotileUsePerSideOuterGap() != enabled) {
        m_settings->setAutotileUsePerSideOuterGap(enabled);
        Q_EMIT autotileUsePerSideOuterGapChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setAutotileOuterGapTop(int gap)
{
    gap = qBound(0, gap, Defaults::MaxGap);
    if (m_settings->autotileOuterGapTop() != gap) {
        m_settings->setAutotileOuterGapTop(gap);
        Q_EMIT autotileOuterGapTopChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setAutotileOuterGapBottom(int gap)
{
    gap = qBound(0, gap, Defaults::MaxGap);
    if (m_settings->autotileOuterGapBottom() != gap) {
        m_settings->setAutotileOuterGapBottom(gap);
        Q_EMIT autotileOuterGapBottomChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setAutotileOuterGapLeft(int gap)
{
    gap = qBound(0, gap, Defaults::MaxGap);
    if (m_settings->autotileOuterGapLeft() != gap) {
        m_settings->setAutotileOuterGapLeft(gap);
        Q_EMIT autotileOuterGapLeftChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setAutotileOuterGapRight(int gap)
{
    gap = qBound(0, gap, Defaults::MaxGap);
    if (m_settings->autotileOuterGapRight() != gap) {
        m_settings->setAutotileOuterGapRight(gap);
        Q_EMIT autotileOuterGapRightChanged();
        setNeedsSave(true);
    }
}

// Editor shortcuts setters (write directly to KConfig Editor group)
// Note: Save, Delete, Close shortcuts now use Qt StandardKey (system shortcuts)
void KCMPlasmaZones::setEditorDuplicateShortcut(const QString& shortcut)
{
    if (editorDuplicateShortcut() != shortcut) {
        KConfigGroup group = editorConfigGroup();
        group.writeEntry(QLatin1String("EditorDuplicateShortcut"), shortcut);
        group.sync();
        Q_EMIT editorDuplicateShortcutChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setEditorSplitHorizontalShortcut(const QString& shortcut)
{
    if (editorSplitHorizontalShortcut() != shortcut) {
        KConfigGroup group = editorConfigGroup();
        group.writeEntry(QLatin1String("EditorSplitHorizontalShortcut"), shortcut);
        group.sync();
        Q_EMIT editorSplitHorizontalShortcutChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setEditorSplitVerticalShortcut(const QString& shortcut)
{
    if (editorSplitVerticalShortcut() != shortcut) {
        KConfigGroup group = editorConfigGroup();
        group.writeEntry(QLatin1String("EditorSplitVerticalShortcut"), shortcut);
        group.sync();
        Q_EMIT editorSplitVerticalShortcutChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setEditorFillShortcut(const QString& shortcut)
{
    if (editorFillShortcut() != shortcut) {
        KConfigGroup group = editorConfigGroup();
        group.writeEntry(QLatin1String("EditorFillShortcut"), shortcut);
        group.sync();
        Q_EMIT editorFillShortcutChanged();
        setNeedsSave(true);
    }
}

// Editor snapping settings setters (write to KConfig Editor group)
void KCMPlasmaZones::setEditorGridSnappingEnabled(bool enabled)
{
    if (editorGridSnappingEnabled() != enabled) {
        KConfigGroup group = editorConfigGroup();
        group.writeEntry(QLatin1String("GridSnappingEnabled"), enabled);
        group.sync();
        Q_EMIT editorGridSnappingEnabledChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setEditorEdgeSnappingEnabled(bool enabled)
{
    if (editorEdgeSnappingEnabled() != enabled) {
        KConfigGroup group = editorConfigGroup();
        group.writeEntry(QLatin1String("EdgeSnappingEnabled"), enabled);
        group.sync();
        Q_EMIT editorEdgeSnappingEnabledChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setEditorSnapIntervalX(qreal interval)
{
    interval = qBound(0.01, interval, 1.0);
    if (!qFuzzyCompare(editorSnapIntervalX(), interval)) {
        KConfigGroup group = editorConfigGroup();
        group.writeEntry(QLatin1String("SnapIntervalX"), interval);
        group.sync();
        Q_EMIT editorSnapIntervalXChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setEditorSnapIntervalY(qreal interval)
{
    interval = qBound(0.01, interval, 1.0);
    if (!qFuzzyCompare(editorSnapIntervalY(), interval)) {
        KConfigGroup group = editorConfigGroup();
        group.writeEntry(QLatin1String("SnapIntervalY"), interval);
        group.sync();
        Q_EMIT editorSnapIntervalYChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setEditorSnapOverrideModifier(int modifier)
{
    if (editorSnapOverrideModifier() != modifier) {
        KConfigGroup group = editorConfigGroup();
        group.writeEntry(QLatin1String("SnapOverrideModifier"), modifier);
        group.sync();
        Q_EMIT editorSnapOverrideModifierChanged();
        setNeedsSave(true);
    }
}

// Fill on drop setters (write to KConfig Editor group)
void KCMPlasmaZones::setFillOnDropEnabled(bool enabled)
{
    if (fillOnDropEnabled() != enabled) {
        KConfigGroup group = editorConfigGroup();
        group.writeEntry(QLatin1String("FillOnDropEnabled"), enabled);
        group.sync();
        Q_EMIT fillOnDropEnabledChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setFillOnDropModifier(int modifier)
{
    if (fillOnDropModifier() != modifier) {
        KConfigGroup group = editorConfigGroup();
        group.writeEntry(QLatin1String("FillOnDropModifier"), modifier);
        group.sync();
        Q_EMIT fillOnDropModifierChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::save()
{
    // Guard against re-entry during synchronous D-Bus operations
    if (m_saveInProgress) {
        qCWarning(lcKcm) << "Save already in progress, ignoring duplicate request";
        return;
    }
    m_saveInProgress = true;
    m_layoutManager->setSaveInProgress(true);

    m_settings->save();

    QStringList failedOperations;

    // All assignment-related saves (screen, desktop, activity, quick slots, KConfig, app rules)
    m_assignmentManager->save(failedOperations);

    // Layout visibility + auto-assign
    m_layoutManager->savePendingStates(failedOperations);

    if (!failedOperations.isEmpty()) {
        qCWarning(lcKcm) << "Save: D-Bus operations failed:" << failedOperations.join(QStringLiteral(", "))
                         << "- some settings may not have been saved to daemon";
    }

    // Notify daemon to reload settings after all D-Bus operations complete.
    // Daemon-side effectiveMaxWindows() and MaxWindows injection handle
    // per-screen algorithm differences without needing synchronous ordering.
    notifyDaemon();

    setNeedsSave(false);
    m_saveInProgress = false;
    m_layoutManager->setSaveInProgress(false);
}

QVariantList KCMPlasmaZones::convertTriggersForQml(const QVariantList& triggers)
{
    QVariantList result;
    for (const auto& t : triggers) {
        auto map = t.toMap();
        QVariantMap converted;
        converted[QStringLiteral("modifier")] = ModifierUtils::dragModifierToBitmask(
            map.value(QStringLiteral("modifier"), 0).toInt());
        converted[QStringLiteral("mouseButton")] = map.value(QStringLiteral("mouseButton"), 0);
        result.append(converted);
    }
    return result;
}

QVariantList KCMPlasmaZones::convertTriggersForStorage(const QVariantList& triggers)
{
    QVariantList result;
    for (const auto& t : triggers) {
        auto map = t.toMap();
        QVariantMap stored;
        stored[QStringLiteral("modifier")] = ModifierUtils::bitmaskToDragModifier(
            map.value(QStringLiteral("modifier"), 0).toInt());
        stored[QStringLiteral("mouseButton")] = map.value(QStringLiteral("mouseButton"), 0);
        result.append(stored);
    }
    return result;
}

void KCMPlasmaZones::load()
{
    m_settings->load();
    // Emit Settings-backed property signals so UI bindings re-evaluate (e.g. after external config change)
    Q_EMIT alwaysActivateOnDragChanged();
    Q_EMIT snappingEnabledChanged();
    Q_EMIT zoneSpanEnabledChanged();
    Q_EMIT snapAssistFeatureEnabledChanged();
    Q_EMIT snapAssistEnabledChanged();
    Q_EMIT snapAssistTriggersChanged();
    Q_EMIT animationSequenceModeChanged();
    Q_EMIT animationStaggerIntervalChanged();
    m_layoutManager->loadSync();
    refreshScreens();

    // Load all assignments (screen, desktop, activity, quick slots, view mode)
    m_assignmentManager->load();
    m_layoutManager->clearPendingStates();

    Q_EMIT screenAssignmentsChanged();
    Q_EMIT tilingScreenAssignmentsChanged();
    Q_EMIT activityAssignmentsChanged();
    Q_EMIT tilingActivityAssignmentsChanged();
    Q_EMIT tilingDesktopAssignmentsChanged();
    Q_EMIT assignmentViewModeChanged();
    Q_EMIT appRulesRefreshed();
    setNeedsSave(false);
}

void KCMPlasmaZones::defaults()
{
    m_settings->reset();

    // Set default layout: pick the layout with the lowest defaultOrder (Columns (2) has 0)
    int bestOrder = 999;
    QString bestId;
    for (const QVariant& layoutVar : m_layoutManager->layouts()) {
        const QVariantMap layout = layoutVar.toMap();
        int order = layout.value(QStringLiteral("defaultOrder"), 999).toInt();
        if (order < bestOrder) {
            bestOrder = order;
            bestId = layout.value(QStringLiteral("id")).toString();
        }
    }
    if (!bestId.isEmpty()) {
        m_settings->setDefaultLayoutId(bestId);
    }

    // Reset all assignments (screen, desktop, activity, quick slots, view mode, pending state)
    m_assignmentManager->resetToDefaults();
    m_layoutManager->resetAllToDefaults();

    // Stage empty app rules for all layouts (clears any daemon-side rules on Apply)
    for (const QVariant& v : m_layoutManager->layouts()) {
        QString layoutId = v.toMap().value(QStringLiteral("id")).toString();
        if (!layoutId.isEmpty()) {
            m_assignmentManager->setAppRulesForLayout(layoutId, QVariantList());
        }
    }
    Q_EMIT appRulesRefreshed();

    // Emit all property change signals so UI updates
    Q_EMIT screenAssignmentsChanged();
    Q_EMIT quickLayoutSlotsChanged();
    Q_EMIT tilingQuickLayoutSlotsChanged();
    Q_EMIT activityAssignmentsChanged();
    emitAllSettingsPropertyChanged();

    // Reset editor shortcuts, assignment view mode, and legacy config groups in one config open
    {
        auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));

        // Clear assignment view mode
        KConfigGroup general = config->group(QStringLiteral("General"));
        general.deleteEntry(QStringLiteral("AssignmentViewMode"));

        // Reset editor shortcuts to defaults
        KConfigGroup editorGroup = config->group(QStringLiteral("Editor"));
        editorGroup.deleteGroup();

        // Clean up legacy config groups (no longer used - daemon owns this data)
        for (const auto& legacyName : {QStringLiteral("ScreenAssignments"),
                                        QStringLiteral("QuickLayoutSlots"),
                                        QStringLiteral("SnappingScreenAssignments"),
                                        QStringLiteral("TilingScreenAssignments"),
                                        QStringLiteral("TilingQuickLayoutSlots")}) {
            KConfigGroup legacy = config->group(legacyName);
            if (legacy.exists()) {
                legacy.deleteGroup();
            }
        }

        // Clean up per-screen assignment groups
        const QStringList allGroupsForClean = config->groupList();
        for (const QString& groupName : allGroupsForClean) {
            if (groupName.startsWith(QLatin1String("SnappingScreen:")) ||
                groupName.startsWith(QLatin1String("TilingScreen:")) ||
                groupName.startsWith(QLatin1String("TilingActivity:")) ||
                groupName.startsWith(QLatin1String("TilingDesktop:"))) {
                config->deleteGroup(groupName);
            }
        }

        config->sync();
    }

    // Emit editor shortcut change signals (app-specific shortcuts only)
    Q_EMIT editorDuplicateShortcutChanged();
    Q_EMIT editorSplitHorizontalShortcutChanged();
    Q_EMIT editorSplitVerticalShortcutChanged();
    Q_EMIT editorFillShortcutChanged();
    Q_EMIT editorGridSnappingEnabledChanged();
    Q_EMIT editorEdgeSnappingEnabledChanged();
    Q_EMIT editorSnapIntervalXChanged();
    Q_EMIT editorSnapIntervalYChanged();
    Q_EMIT editorSnapOverrideModifierChanged();
    Q_EMIT fillOnDropEnabledChanged();
    Q_EMIT fillOnDropModifierChanged();

    setNeedsSave(true);
}

// Layout CRUD delegates (implementation in LayoutManager)
void KCMPlasmaZones::createNewLayout() { m_layoutManager->createNewLayout(); }
void KCMPlasmaZones::deleteLayout(const QString& layoutId) { m_layoutManager->deleteLayout(layoutId); }
void KCMPlasmaZones::duplicateLayout(const QString& layoutId) { m_layoutManager->duplicateLayout(layoutId); }
void KCMPlasmaZones::importLayout(const QString& filePath) { m_layoutManager->importLayout(filePath); }
void KCMPlasmaZones::exportLayout(const QString& layoutId, const QString& filePath) { m_layoutManager->exportLayout(layoutId, filePath); }
void KCMPlasmaZones::editLayout(const QString& layoutId) { m_layoutManager->editLayout(layoutId); }
void KCMPlasmaZones::openEditor() { m_layoutManager->openEditor(); }
void KCMPlasmaZones::setLayoutHidden(const QString& layoutId, bool hidden) { m_layoutManager->setLayoutHidden(layoutId, hidden); }
void KCMPlasmaZones::setLayoutAutoAssign(const QString& layoutId, bool enabled) { m_layoutManager->setLayoutAutoAssign(layoutId, enabled); }

void KCMPlasmaZones::addExcludedApp(const QString& app)
{
    auto apps = m_settings->excludedApplications();
    if (!apps.contains(app)) {
        apps.append(app);
        setExcludedApplications(apps);
    }
}

void KCMPlasmaZones::removeExcludedApp(int index)
{
    auto apps = m_settings->excludedApplications();
    if (index >= 0 && index < apps.size()) {
        apps.removeAt(index);
        setExcludedApplications(apps);
    }
}

void KCMPlasmaZones::addExcludedWindowClass(const QString& windowClass)
{
    auto classes = m_settings->excludedWindowClasses();
    if (!classes.contains(windowClass)) {
        classes.append(windowClass);
        setExcludedWindowClasses(classes);
    }
}

void KCMPlasmaZones::removeExcludedWindowClass(int index)
{
    auto classes = m_settings->excludedWindowClasses();
    if (index >= 0 && index < classes.size()) {
        classes.removeAt(index);
        setExcludedWindowClasses(classes);
    }
}

QVariantList KCMPlasmaZones::getRunningWindows()
{
    QDBusMessage reply = callDaemon(QString(DBus::Interface::Settings), QStringLiteral("getRunningWindows"));

    if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty()) {
        return {};
    }

    QString json = reply.arguments().at(0).toString();
    if (json.isEmpty()) {
        return {};
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        return {};
    }

    QVariantList result;
    const QJsonArray array = doc.array();
    for (const QJsonValue& value : array) {
        if (!value.isObject()) {
            continue;
        }
        QJsonObject obj = value.toObject();
        QVariantMap item;
        item[QStringLiteral("windowClass")] = obj[QStringLiteral("windowClass")].toString();
        item[QStringLiteral("appName")] = obj[QStringLiteral("appName")].toString();
        item[QStringLiteral("caption")] = obj[QStringLiteral("caption")].toString();
        result.append(item);
    }

    return result;
}

QStringList KCMPlasmaZones::fontStylesForFamily(const QString& family) const
{
    return QFontDatabase::styles(family);
}

int KCMPlasmaZones::fontStyleWeight(const QString& family, const QString& style) const
{
    return QFontDatabase::weight(family, style);
}

bool KCMPlasmaZones::fontStyleItalic(const QString& family, const QString& style) const
{
    return QFontDatabase::italic(family, style);
}

void KCMPlasmaZones::loadColorsFromPywal()
{
    QString pywalPath = QDir::homePath() + QStringLiteral("/.cache/wal/colors.json");
    if (!QFile::exists(pywalPath)) {
        Q_EMIT colorImportError(tr("Pywal colors not found. Run 'wal' to generate colors first.\n\nExpected file: %1").arg(pywalPath));
        return;
    }

    QString error = m_settings->loadColorsFromFile(pywalPath);
    if (!error.isEmpty()) {
        Q_EMIT colorImportError(error);
        return;
    }

    Q_EMIT highlightColorChanged();
    Q_EMIT inactiveColorChanged();
    Q_EMIT borderColorChanged();
    Q_EMIT labelFontColorChanged();
    Q_EMIT useSystemColorsChanged();
    Q_EMIT colorImportSuccess();
    setNeedsSave(true);
}

void KCMPlasmaZones::loadColorsFromFile(const QString& filePath)
{
    QString error = m_settings->loadColorsFromFile(filePath);
    if (!error.isEmpty()) {
        Q_EMIT colorImportError(error);
        return;
    }

    Q_EMIT highlightColorChanged();
    Q_EMIT inactiveColorChanged();
    Q_EMIT borderColorChanged();
    Q_EMIT labelFontColorChanged();
    Q_EMIT useSystemColorsChanged();
    Q_EMIT colorImportSuccess();
    setNeedsSave(true);
}

void KCMPlasmaZones::resetEditorShortcuts()
{
    // Force set all app-specific editor shortcuts to defaults (always emit signals)
    // Note: Save, Delete, Close shortcuts use Qt StandardKey (system shortcuts) and are not configurable
    KConfigGroup group = editorConfigGroup();

    group.writeEntry(QLatin1String("EditorDuplicateShortcut"), QStringLiteral("Ctrl+D"));
    group.writeEntry(QLatin1String("EditorSplitHorizontalShortcut"), QStringLiteral("Ctrl+Shift+H"));
    group.writeEntry(
        QLatin1String("EditorSplitVerticalShortcut"),
        QStringLiteral("Ctrl+Alt+V")); // Note: Changed from Ctrl+Shift+V to avoid conflict with Paste with Offset
    group.writeEntry(QLatin1String("EditorFillShortcut"), QStringLiteral("Ctrl+Shift+F"));

    group.sync();

    // Always emit signals to update UI
    Q_EMIT editorDuplicateShortcutChanged();
    Q_EMIT editorSplitHorizontalShortcutChanged();
    Q_EMIT editorSplitVerticalShortcutChanged();
    Q_EMIT editorFillShortcutChanged();

    setNeedsSave(true);
}

// Daemon status delegates (implementation in DaemonController)
bool KCMPlasmaZones::isDaemonRunning() const
{
    return m_daemonController->isRunning();
}

bool KCMPlasmaZones::isDaemonEnabled() const
{
    return m_daemonController->isEnabled();
}

void KCMPlasmaZones::setDaemonEnabled(bool enabled)
{
    m_daemonController->setEnabled(enabled);
}

void KCMPlasmaZones::startDaemon()
{
    m_daemonController->setEnabled(true);
}

void KCMPlasmaZones::stopDaemon()
{
    m_daemonController->setEnabled(false);
}

// Update checker methods
bool KCMPlasmaZones::updateAvailable() const
{
    return m_updateChecker ? m_updateChecker->updateAvailable() : false;
}

QString KCMPlasmaZones::currentVersion() const
{
    return VERSION_STRING;
}

QString KCMPlasmaZones::latestVersion() const
{
    return m_updateChecker ? m_updateChecker->latestVersion() : QString();
}

QString KCMPlasmaZones::releaseUrl() const
{
    return m_updateChecker ? m_updateChecker->releaseUrl() : QString();
}

bool KCMPlasmaZones::checkingForUpdates() const
{
    return m_updateChecker ? m_updateChecker->isChecking() : false;
}

QString KCMPlasmaZones::dismissedUpdateVersion() const
{
    return m_dismissedUpdateVersion;
}

void KCMPlasmaZones::setDismissedUpdateVersion(const QString& version)
{
    if (m_dismissedUpdateVersion != version) {
        m_dismissedUpdateVersion = version;

        // Persist to config
        KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
        KConfigGroup group = config->group(QStringLiteral("Updates"));
        group.writeEntry("DismissedUpdateVersion", version);
        config->sync();

        Q_EMIT dismissedUpdateVersionChanged();
    }
}

void KCMPlasmaZones::checkForUpdates()
{
    if (m_updateChecker) {
        m_updateChecker->checkForUpdates();
    }
}

void KCMPlasmaZones::openReleaseUrl()
{
    QString url = releaseUrl();
    if (!url.isEmpty()) {
        QDesktopServices::openUrl(QUrl(url));
    } else {
        // Fallback to main releases page
        QDesktopServices::openUrl(QUrl(GITHUB_RELEASES_URL));
    }
}

void KCMPlasmaZones::scheduleLoadLayouts()
{
    m_layoutManager->scheduleLoad();
}

void KCMPlasmaZones::emitAllSettingsPropertyChanged()
{
    // Activation
    Q_EMIT dragActivationTriggersChanged();
    Q_EMIT alwaysActivateOnDragChanged();
    Q_EMIT zoneSpanEnabledChanged();
    Q_EMIT zoneSpanTriggersChanged();
    Q_EMIT toggleActivationChanged();
    Q_EMIT snappingEnabledChanged();

    // Display
    Q_EMIT showZonesOnAllMonitorsChanged();
    Q_EMIT disabledMonitorsChanged();
    Q_EMIT showZoneNumbersChanged();
    Q_EMIT flashZonesOnSwitchChanged();
    Q_EMIT showOsdOnLayoutSwitchChanged();
    Q_EMIT showNavigationOsdChanged();
    Q_EMIT osdStyleChanged();

    // Appearance
    Q_EMIT useSystemColorsChanged();
    Q_EMIT highlightColorChanged();
    Q_EMIT inactiveColorChanged();
    Q_EMIT borderColorChanged();
    Q_EMIT labelFontColorChanged();
    Q_EMIT activeOpacityChanged();
    Q_EMIT inactiveOpacityChanged();
    Q_EMIT borderWidthChanged();
    Q_EMIT borderRadiusChanged();
    Q_EMIT enableBlurChanged();
    Q_EMIT labelFontFamilyChanged();
    Q_EMIT labelFontSizeScaleChanged();
    Q_EMIT labelFontWeightChanged();
    Q_EMIT labelFontItalicChanged();
    Q_EMIT labelFontUnderlineChanged();
    Q_EMIT labelFontStrikeoutChanged();
    Q_EMIT enableShaderEffectsChanged();
    Q_EMIT shaderFrameRateChanged();
    Q_EMIT enableAudioVisualizerChanged();
    Q_EMIT audioSpectrumBarCountChanged();

    // Zones
    Q_EMIT zonePaddingChanged();
    Q_EMIT outerGapChanged();
    Q_EMIT usePerSideOuterGapChanged();
    Q_EMIT outerGapTopChanged();
    Q_EMIT outerGapBottomChanged();
    Q_EMIT outerGapLeftChanged();
    Q_EMIT outerGapRightChanged();
    Q_EMIT adjacentThresholdChanged();

    // Behavior
    Q_EMIT keepWindowsInZonesOnResolutionChangeChanged();
    Q_EMIT moveNewWindowsToLastZoneChanged();
    Q_EMIT restoreOriginalSizeOnUnsnapChanged();
    Q_EMIT stickyWindowHandlingChanged();
    Q_EMIT restoreWindowsToZonesOnLoginChanged();
    Q_EMIT snapAssistFeatureEnabledChanged();
    Q_EMIT snapAssistEnabledChanged();
    Q_EMIT snapAssistTriggersChanged();
    Q_EMIT defaultLayoutIdChanged();

    // Exclusions
    Q_EMIT excludedApplicationsChanged();
    Q_EMIT excludedWindowClassesChanged();
    Q_EMIT excludeTransientWindowsChanged();
    Q_EMIT minimumWindowWidthChanged();
    Q_EMIT minimumWindowHeightChanged();

    // Zone Selector
    Q_EMIT zoneSelectorEnabledChanged();
    Q_EMIT zoneSelectorTriggerDistanceChanged();
    Q_EMIT zoneSelectorPositionChanged();
    Q_EMIT zoneSelectorLayoutModeChanged();
    Q_EMIT zoneSelectorPreviewWidthChanged();
    Q_EMIT zoneSelectorPreviewHeightChanged();
    Q_EMIT zoneSelectorPreviewLockAspectChanged();
    Q_EMIT zoneSelectorGridColumnsChanged();
    Q_EMIT zoneSelectorSizeModeChanged();
    Q_EMIT zoneSelectorMaxRowsChanged();

    // Autotiling
    Q_EMIT autotileEnabledChanged();
    Q_EMIT autotileAlgorithmChanged();
    Q_EMIT autotileSplitRatioChanged();
    Q_EMIT autotileMasterCountChanged();
    Q_EMIT autotileInnerGapChanged();
    Q_EMIT autotileOuterGapChanged();
    Q_EMIT autotileFocusNewWindowsChanged();
    Q_EMIT autotileSmartGapsChanged();
    Q_EMIT autotileMaxWindowsChanged();
    Q_EMIT autotileInsertPositionChanged();
    Q_EMIT animationsEnabledChanged();
    Q_EMIT animationDurationChanged();
    Q_EMIT animationEasingCurveChanged();
    Q_EMIT animationMinDistanceChanged();
    Q_EMIT animationSequenceModeChanged();
    Q_EMIT animationStaggerIntervalChanged();
    Q_EMIT autotileFocusFollowsMouseChanged();
    Q_EMIT autotileRespectMinimumSizeChanged();
    Q_EMIT autotileHideTitleBarsChanged();
    Q_EMIT autotileBorderWidthChanged();
    Q_EMIT autotileBorderColorChanged();
    Q_EMIT autotileUseSystemBorderColorsChanged();
    Q_EMIT autotileUsePerSideOuterGapChanged();
    Q_EMIT autotileOuterGapTopChanged();
    Q_EMIT autotileOuterGapBottomChanged();
    Q_EMIT autotileOuterGapLeftChanged();
    Q_EMIT autotileOuterGapRightChanged();
}

void KCMPlasmaZones::onSettingsChanged()
{
    // When settings change externally (e.g., via D-Bus from another process),
    // reload settings from the config file
    if (m_settings) {
        m_settings->load();
        emitAllSettingsPropertyChanged();
    }
}

QDBusMessage KCMPlasmaZones::callDaemon(const QString& interface, const QString& method, const QVariantList& args) const
{
    QDBusMessage msg =
        QDBusMessage::createMethodCall(QString(DBus::ServiceName), QString(DBus::ObjectPath), interface, method);

    if (!args.isEmpty()) {
        msg.setArguments(args);
    }

    // Use synchronous call with timeout to prevent UI freeze on unresponsive daemon
    QDBusMessage reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, DBusTimeoutMs);

    if (reply.type() == QDBusMessage::ErrorMessage) {
        qCWarning(lcKcm) << "D-Bus call failed:" << interface << "::" << method << "-" << reply.errorName() << ":"
                         << reply.errorMessage();
    }

    return reply;
}

void KCMPlasmaZones::watchAsyncDbusCall(QDBusPendingCall call, const QString& operation)
{
    auto* watcher = new QDBusPendingCallWatcher(std::move(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [operation](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        QDBusPendingReply<> reply = *w;
        if (reply.isError()) {
            qCWarning(lcKcm) << operation << "D-Bus call failed:" << reply.error().message();
        }
    });
}

QString KCMPlasmaZones::currentScreenName() const
{
    if (auto* window = QGuiApplication::focusWindow()) {
        if (auto* screen = window->screen()) {
            return screen->name();
        }
    }
    return QString();
}

void KCMPlasmaZones::notifyDaemon()
{
    // Notify daemon to reload settings via the SettingsAdaptor interface.
    // Async: the daemon-side effectiveMaxWindows() and MaxWindows injection
    // in updateAutotileScreens() handle per-screen algorithm differences,
    // so strict ordering before assignment D-Bus calls is not required.
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QString(DBus::ServiceName), QString(DBus::ObjectPath),
        QString(DBus::Interface::Settings), QStringLiteral("reloadSettings"));
    watchAsyncDbusCall(QDBusConnection::sessionBus().asyncCall(msg),
                       QStringLiteral("notifyDaemon(reloadSettings)"));
}

void KCMPlasmaZones::refreshScreens()
{
    QVariantList newScreens;

    // Get primary screen name from daemon for isPrimary flag
    QString primaryScreenName;
    QDBusMessage primaryReply = callDaemon(QString(DBus::Interface::Screen), QStringLiteral("getPrimaryScreen"));
    if (primaryReply.type() == QDBusMessage::ReplyMessage && !primaryReply.arguments().isEmpty()) {
        primaryScreenName = primaryReply.arguments().first().toString();
    }

    // Get screens from daemon via D-Bus
    QDBusMessage screenReply = callDaemon(QString(DBus::Interface::Screen), QStringLiteral("getScreens"));

    if (screenReply.type() == QDBusMessage::ReplyMessage && !screenReply.arguments().isEmpty()) {
        QStringList screenNames = screenReply.arguments().first().toStringList();

        for (const QString& screenName : screenNames) {
            // Get screen info
            QDBusMessage infoReply =
                callDaemon(QString(DBus::Interface::Screen), QStringLiteral("getScreenInfo"), {screenName});

            if (infoReply.type() == QDBusMessage::ReplyMessage && !infoReply.arguments().isEmpty()) {
                QString infoJson = infoReply.arguments().first().toString();
                QJsonDocument doc = QJsonDocument::fromJson(infoJson.toUtf8());
                if (!doc.isNull() && doc.isObject()) {
                    QJsonObject jsonObj = doc.object();
                    QVariantMap screenInfo;
                    screenInfo[QStringLiteral("name")] = screenName;
                    screenInfo[QStringLiteral("isPrimary")] = (screenName == primaryScreenName);

                    // Include stable EDID-based screen ID from daemon
                    if (jsonObj.contains(QStringLiteral("screenId"))) {
                        screenInfo[QStringLiteral("screenId")] = jsonObj[QStringLiteral("screenId")].toString();
                    } else {
                        screenInfo[QStringLiteral("screenId")] = screenName;
                    }

                    // Forward manufacturer/model for display
                    if (jsonObj.contains(QStringLiteral("manufacturer"))) {
                        screenInfo[QStringLiteral("manufacturer")] = jsonObj[QStringLiteral("manufacturer")].toString();
                    }
                    if (jsonObj.contains(QStringLiteral("model"))) {
                        screenInfo[QStringLiteral("model")] = jsonObj[QStringLiteral("model")].toString();
                    }

                    // Create resolution string from geometry for QML display
                    if (jsonObj.contains(QStringLiteral("geometry"))) {
                        QJsonObject geom = jsonObj[QStringLiteral("geometry")].toObject();
                        int width = geom[QStringLiteral("width")].toInt();
                        int height = geom[QStringLiteral("height")].toInt();
                        screenInfo[QStringLiteral("resolution")] = QStringLiteral("%1×%2").arg(width).arg(height);
                    }

                    newScreens.append(screenInfo);
                } else {
                    // If JSON parsing fails, create minimal screen info
                    QVariantMap screenInfo;
                    screenInfo[QStringLiteral("name")] = screenName;
                    screenInfo[QStringLiteral("isPrimary")] = (screenName == primaryScreenName);
                    newScreens.append(screenInfo);
                }
            } else {
                // If D-Bus call fails, create minimal screen info
                QVariantMap screenInfo;
                screenInfo[QStringLiteral("name")] = screenName;
                screenInfo[QStringLiteral("isPrimary")] = (screenName == primaryScreenName);
                newScreens.append(screenInfo);
            }
        }
    }

    // Fallback: if no screens from daemon, get from Qt
    if (newScreens.isEmpty()) {
        QScreen* primaryScreen = QGuiApplication::primaryScreen();
        for (QScreen* screen : QGuiApplication::screens()) {
            QVariantMap screenInfo;
            screenInfo[QStringLiteral("name")] = screen->name();
            screenInfo[QStringLiteral("isPrimary")] = (screen == primaryScreen);
            screenInfo[QStringLiteral("resolution")] = QStringLiteral("%1×%2")
                .arg(screen->geometry().width())
                .arg(screen->geometry().height());
            screenInfo[QStringLiteral("manufacturer")] = screen->manufacturer();
            screenInfo[QStringLiteral("model")] = screen->model();
            screenInfo[QStringLiteral("screenId")] = screen->name();
            newScreens.append(screenInfo);
        }
    }

    m_screens = newScreens;

    // Note: Screen assignments and quick layout slots are loaded from config in load()
    // We don't overwrite them here to preserve pending changes

    Q_EMIT screensChanged();
}

void KCMPlasmaZones::refreshVirtualDesktops()
{
    int newCount = 1;
    QStringList newNames;

    // Query daemon for virtual desktop count
    QDBusMessage countReply =
        callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("getVirtualDesktopCount"));

    if (countReply.type() == QDBusMessage::ReplyMessage && !countReply.arguments().isEmpty()) {
        newCount = countReply.arguments().first().toInt();
        if (newCount < 1) {
            newCount = 1;
        }
    }

    // Query daemon for virtual desktop names
    QDBusMessage namesReply =
        callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("getVirtualDesktopNames"));

    if (namesReply.type() == QDBusMessage::ReplyMessage && !namesReply.arguments().isEmpty()) {
        newNames = namesReply.arguments().first().toStringList();
    }

    // Fallback if daemon not available
    if (newNames.isEmpty()) {
        for (int i = 1; i <= newCount; ++i) {
            newNames.append(QStringLiteral("Desktop %1").arg(i));
        }
    }

    // Update and emit signals if changed
    if (m_virtualDesktopCount != newCount) {
        m_virtualDesktopCount = newCount;
        Q_EMIT virtualDesktopCountChanged();
    }

    if (m_virtualDesktopNames != newNames) {
        m_virtualDesktopNames = newNames;
        Q_EMIT virtualDesktopNamesChanged();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// KDE Activities Support
// ═══════════════════════════════════════════════════════════════════════════════

void KCMPlasmaZones::refreshActivities()
{
    bool wasAvailable = m_activitiesAvailable;
    QVariantList oldActivities = m_activities;
    QString oldCurrentActivity = m_currentActivity;

    // Query daemon for activities availability
    QDBusMessage availReply =
        callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("isActivitiesAvailable"));

    if (availReply.type() == QDBusMessage::ReplyMessage && !availReply.arguments().isEmpty()) {
        m_activitiesAvailable = availReply.arguments().first().toBool();
    } else {
        m_activitiesAvailable = false;
    }

    if (m_activitiesAvailable) {
        // Query daemon for activities list (JSON array)
        QDBusMessage activitiesReply =
            callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("getAllActivitiesInfo"));

        if (activitiesReply.type() == QDBusMessage::ReplyMessage && !activitiesReply.arguments().isEmpty()) {
            QString jsonStr = activitiesReply.arguments().first().toString();
            QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());
            if (doc.isArray()) {
                m_activities.clear();
                const QJsonArray arr = doc.array();
                for (const QJsonValue& val : arr) {
                    QJsonObject obj = val.toObject();
                    QVariantMap activity;
                    activity[QStringLiteral("id")] = obj[QLatin1String("id")].toString();
                    activity[QStringLiteral("name")] = obj[QLatin1String("name")].toString();
                    activity[QStringLiteral("icon")] = obj[QLatin1String("icon")].toString();
                    m_activities.append(activity);
                }
            }
        }

        // Query current activity
        QDBusMessage currentReply =
            callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("getCurrentActivity"));

        if (currentReply.type() == QDBusMessage::ReplyMessage && !currentReply.arguments().isEmpty()) {
            m_currentActivity = currentReply.arguments().first().toString();
        }
    } else {
        m_activities.clear();
        m_currentActivity.clear();
    }

    // Emit signals if changed
    if (wasAvailable != m_activitiesAvailable) {
        Q_EMIT activitiesAvailableChanged();
    }
    if (oldActivities != m_activities) {
        Q_EMIT activitiesChanged();
    }
    if (oldCurrentActivity != m_currentActivity) {
        Q_EMIT currentActivityChanged();
    }
}

bool KCMPlasmaZones::activitiesAvailable() const
{
    return m_activitiesAvailable;
}

QVariantList KCMPlasmaZones::activities() const
{
    return m_activities;
}

QString KCMPlasmaZones::currentActivity() const
{
    return m_currentActivity;
}

void KCMPlasmaZones::onCurrentActivityChanged(const QString& activityId)
{
    if (m_currentActivity != activityId) {
        m_currentActivity = activityId;
        Q_EMIT currentActivityChanged();
    }
}

void KCMPlasmaZones::onActivitiesChanged()
{
    refreshActivities();
}

void KCMPlasmaZones::assignLayoutToScreenActivity(const QString& screenName, const QString& activityId,
                                                   const QString& layoutId)
{
    m_assignmentManager->assignLayoutToScreenActivity(screenName, activityId, layoutId);
}

void KCMPlasmaZones::clearScreenActivityAssignment(const QString& screenName, const QString& activityId)
{
    m_assignmentManager->clearScreenActivityAssignment(screenName, activityId);
}

QString KCMPlasmaZones::getLayoutForScreenActivity(const QString& screenName, const QString& activityId) const
{
    return m_assignmentManager->getLayoutForScreenActivity(screenName, activityId);
}

bool KCMPlasmaZones::hasExplicitAssignmentForScreenActivity(const QString& screenName, const QString& activityId) const
{
    return m_assignmentManager->hasExplicitAssignmentForScreenActivity(screenName, activityId);
}

void KCMPlasmaZones::assignTilingLayoutToScreenActivity(const QString& screenName, const QString& activityId,
                                                         const QString& layoutId)
{
    m_assignmentManager->assignTilingLayoutToScreenActivity(screenName, activityId, layoutId);
}

void KCMPlasmaZones::clearTilingScreenActivityAssignment(const QString& screenName, const QString& activityId)
{
    m_assignmentManager->clearTilingScreenActivityAssignment(screenName, activityId);
}

QString KCMPlasmaZones::getTilingLayoutForScreenActivity(const QString& screenName, const QString& activityId) const
{
    return m_assignmentManager->getTilingLayoutForScreenActivity(screenName, activityId);
}

bool KCMPlasmaZones::hasExplicitTilingAssignmentForScreenActivity(const QString& screenName, const QString& activityId) const
{
    return m_assignmentManager->hasExplicitTilingAssignmentForScreenActivity(screenName, activityId);
}

void KCMPlasmaZones::assignTilingLayoutToScreenDesktop(const QString& screenName, int virtualDesktop,
                                                        const QString& layoutId)
{
    m_assignmentManager->assignTilingLayoutToScreenDesktop(screenName, virtualDesktop, layoutId);
}

void KCMPlasmaZones::clearTilingScreenDesktopAssignment(const QString& screenName, int virtualDesktop)
{
    m_assignmentManager->clearTilingScreenDesktopAssignment(screenName, virtualDesktop);
}

QString KCMPlasmaZones::getTilingLayoutForScreenDesktop(const QString& screenName, int virtualDesktop) const
{
    return m_assignmentManager->getTilingLayoutForScreenDesktop(screenName, virtualDesktop);
}

bool KCMPlasmaZones::hasExplicitTilingAssignmentForScreenDesktop(const QString& screenName, int virtualDesktop) const
{
    return m_assignmentManager->hasExplicitTilingAssignmentForScreenDesktop(screenName, virtualDesktop);
}

void KCMPlasmaZones::assignLayoutToScreen(const QString& screenName, const QString& layoutId)
{
    m_assignmentManager->assignLayoutToScreen(screenName, layoutId);
}

void KCMPlasmaZones::clearScreenAssignment(const QString& screenName)
{
    m_assignmentManager->clearScreenAssignment(screenName);
}

QString KCMPlasmaZones::getLayoutForScreen(const QString& screenName) const
{
    return m_assignmentManager->getLayoutForScreen(screenName);
}

void KCMPlasmaZones::assignTilingLayoutToScreen(const QString& screenName, const QString& layoutId)
{
    m_assignmentManager->assignTilingLayoutToScreen(screenName, layoutId);
}

void KCMPlasmaZones::clearTilingScreenAssignment(const QString& screenName)
{
    m_assignmentManager->clearTilingScreenAssignment(screenName);
}

QString KCMPlasmaZones::getTilingLayoutForScreen(const QString& screenName) const
{
    return m_assignmentManager->getTilingLayoutForScreen(screenName);
}

QString KCMPlasmaZones::getTilingQuickLayoutSlot(int slotNumber) const
{
    return m_assignmentManager->getTilingQuickLayoutSlot(slotNumber);
}

void KCMPlasmaZones::setTilingQuickLayoutSlot(int slotNumber, const QString& layoutId)
{
    m_assignmentManager->setTilingQuickLayoutSlot(slotNumber, layoutId);
}

bool KCMPlasmaZones::isMonitorDisabled(const QString& screenName) const
{
    return m_settings && m_settings->isMonitorDisabled(screenName);
}

void KCMPlasmaZones::setMonitorDisabled(const QString& screenName, bool disabled)
{
    if (!m_settings || screenName.isEmpty()) {
        return;
    }
    // Translate connector name to stable EDID-based screen ID for storage
    QString id = Utils::screenIdForName(screenName);
    QStringList list = m_settings->disabledMonitors();
    if (disabled) {
        if (!list.contains(id)) {
            list.append(id);
            m_settings->setDisabledMonitors(list);
            Q_EMIT disabledMonitorsChanged();
            setNeedsSave(true);
        }
    } else {
        // Remove both screen ID and any legacy connector name entries
        bool changed = list.removeAll(id) > 0;
        if (id != screenName) {
            changed |= list.removeAll(screenName) > 0;
        }
        if (changed) {
            m_settings->setDisabledMonitors(list);
            Q_EMIT disabledMonitorsChanged();
            setNeedsSave(true);
        }
    }
}

// Per-screen settings helpers (DRY: 4 helpers replace 12 near-identical methods)
QVariantMap KCMPlasmaZones::getPerScreenSettingsImpl(const QString& screenName, PerScreenGetter getter) const
{
    return m_settings ? (m_settings->*getter)(Utils::screenIdForName(screenName)) : QVariantMap();
}

void KCMPlasmaZones::setPerScreenSettingImpl(const QString& screenName, const QString& key,
                                              const QVariant& value, PerScreenSetter setter)
{
    if (!m_settings || screenName.isEmpty()) {
        return;
    }
    (m_settings->*setter)(Utils::screenIdForName(screenName), key, value);
    setNeedsSave(true);
}

void KCMPlasmaZones::clearPerScreenSettingsImpl(const QString& screenName, PerScreenClearer clearer)
{
    if (!m_settings || screenName.isEmpty()) {
        return;
    }
    (m_settings->*clearer)(Utils::screenIdForName(screenName));
    setNeedsSave(true);
}

bool KCMPlasmaZones::hasPerScreenSettingsImpl(const QString& screenName, PerScreenChecker checker) const
{
    return m_settings ? (m_settings->*checker)(Utils::screenIdForName(screenName)) : false;
}

// Per-screen zone selector settings
QVariantMap KCMPlasmaZones::getPerScreenZoneSelectorSettings(const QString& screenName) const
{
    return getPerScreenSettingsImpl(screenName, &Settings::getPerScreenZoneSelectorSettings);
}
void KCMPlasmaZones::setPerScreenZoneSelectorSetting(const QString& screenName, const QString& key, const QVariant& value)
{
    setPerScreenSettingImpl(screenName, key, value, &Settings::setPerScreenZoneSelectorSetting);
}
void KCMPlasmaZones::clearPerScreenZoneSelectorSettings(const QString& screenName)
{
    clearPerScreenSettingsImpl(screenName, &Settings::clearPerScreenZoneSelectorSettings);
}
bool KCMPlasmaZones::hasPerScreenZoneSelectorSettings(const QString& screenName) const
{
    return hasPerScreenSettingsImpl(screenName, &Settings::hasPerScreenZoneSelectorSettings);
}

// Per-screen autotile settings
QVariantMap KCMPlasmaZones::getPerScreenAutotileSettings(const QString& screenName) const
{
    return getPerScreenSettingsImpl(screenName, &Settings::getPerScreenAutotileSettings);
}
void KCMPlasmaZones::setPerScreenAutotileSetting(const QString& screenName, const QString& key, const QVariant& value)
{
    setPerScreenSettingImpl(screenName, key, value, &Settings::setPerScreenAutotileSetting);
}
void KCMPlasmaZones::clearPerScreenAutotileSettings(const QString& screenName)
{
    clearPerScreenSettingsImpl(screenName, &Settings::clearPerScreenAutotileSettings);
}
bool KCMPlasmaZones::hasPerScreenAutotileSettings(const QString& screenName) const
{
    return hasPerScreenSettingsImpl(screenName, &Settings::hasPerScreenAutotileSettings);
}

// Per-screen snapping settings
QVariantMap KCMPlasmaZones::getPerScreenSnappingSettings(const QString& screenName) const
{
    return getPerScreenSettingsImpl(screenName, &Settings::getPerScreenSnappingSettings);
}
void KCMPlasmaZones::setPerScreenSnappingSetting(const QString& screenName, const QString& key, const QVariant& value)
{
    setPerScreenSettingImpl(screenName, key, value, &Settings::setPerScreenSnappingSetting);
}
void KCMPlasmaZones::clearPerScreenSnappingSettings(const QString& screenName)
{
    clearPerScreenSettingsImpl(screenName, &Settings::clearPerScreenSnappingSettings);
}
bool KCMPlasmaZones::hasPerScreenSnappingSettings(const QString& screenName) const
{
    return hasPerScreenSettingsImpl(screenName, &Settings::hasPerScreenSnappingSettings);
}

// Returns whether this screen has a tiling assignment in the KCM's in-memory state.
// This reflects KCM state only — not live daemon state.
bool KCMPlasmaZones::isScreenInTilingMode(const QString& screenName) const
{
    return m_assignmentManager->tilingScreenAssignments().contains(screenName);
}

void KCMPlasmaZones::assignLayoutToScreenDesktop(const QString& screenName, int virtualDesktop, const QString& layoutId)
{
    m_assignmentManager->assignLayoutToScreenDesktop(screenName, virtualDesktop, layoutId);
}

void KCMPlasmaZones::clearScreenDesktopAssignment(const QString& screenName, int virtualDesktop)
{
    m_assignmentManager->clearScreenDesktopAssignment(screenName, virtualDesktop);
}

QString KCMPlasmaZones::getLayoutForScreenDesktop(const QString& screenName, int virtualDesktop) const
{
    return m_assignmentManager->getLayoutForScreenDesktop(screenName, virtualDesktop);
}

bool KCMPlasmaZones::hasExplicitAssignmentForScreenDesktop(const QString& screenName, int virtualDesktop) const
{
    return m_assignmentManager->hasExplicitAssignmentForScreenDesktop(screenName, virtualDesktop);
}

QString KCMPlasmaZones::getQuickLayoutSlot(int slotNumber) const
{
    return m_assignmentManager->getQuickLayoutSlot(slotNumber);
}

void KCMPlasmaZones::setQuickLayoutSlot(int slotNumber, const QString& layoutId)
{
    m_assignmentManager->setQuickLayoutSlot(slotNumber, layoutId);
}

QString KCMPlasmaZones::getQuickLayoutShortcut(int slotNumber) const
{
    return m_assignmentManager->getQuickLayoutShortcut(slotNumber);
}

// ═══════════════════════════════════════════════════════════════════════════════
// App-to-Zone Rules
// ═══════════════════════════════════════════════════════════════════════════════

QVariantList KCMPlasmaZones::getAppRulesForLayout(const QString& layoutId) const
{
    return m_assignmentManager->getAppRulesForLayout(layoutId);
}

void KCMPlasmaZones::setAppRulesForLayout(const QString& layoutId, const QVariantList& rules)
{
    m_assignmentManager->setAppRulesForLayout(layoutId, rules);
}

void KCMPlasmaZones::addAppRuleToLayout(const QString& layoutId, const QString& pattern,
                                         int zoneNumber, const QString& targetScreen)
{
    m_assignmentManager->addAppRuleToLayout(layoutId, pattern, zoneNumber, targetScreen);
}

void KCMPlasmaZones::removeAppRuleFromLayout(const QString& layoutId, int index)
{
    m_assignmentManager->removeAppRuleFromLayout(layoutId, index);
}

} // namespace PlasmaZones

#include "kcm_plasmazones.moc"
