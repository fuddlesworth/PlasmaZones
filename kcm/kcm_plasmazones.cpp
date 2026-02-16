// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "kcm_plasmazones.h"
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
    loadLayouts();
    refreshScreens();

    // Set up daemon status polling (fallback for edge cases the watcher might miss)
    m_daemonCheckTimer = new QTimer(this);
    m_daemonCheckTimer->setInterval(KCMConstants::DaemonStatusPollIntervalMs);
    connect(m_daemonCheckTimer, &QTimer::timeout, this, &KCMPlasmaZones::checkDaemonStatus);
    m_daemonCheckTimer->start();

    // Set up D-Bus service watcher for immediate daemon start/stop notification
    m_daemonWatcher = new QDBusServiceWatcher(
        QString(DBus::ServiceName),
        QDBusConnection::sessionBus(),
        QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration,
        this);

    connect(m_daemonWatcher, &QDBusServiceWatcher::serviceRegistered, this, [this]() {
        if (!m_lastDaemonState) {
            m_lastDaemonState = true;
            Q_EMIT daemonRunningChanged();
            // Refresh data from the newly-started daemon
            loadLayouts();
            refreshScreens();
        }
    });

    connect(m_daemonWatcher, &QDBusServiceWatcher::serviceUnregistered, this, [this]() {
        if (m_lastDaemonState) {
            m_lastDaemonState = false;
            Q_EMIT daemonRunningChanged();
        }
    });

    // Load daemon enabled state from systemd (async)
    m_lastDaemonState = isDaemonRunning();
    m_daemonEnabled = m_lastDaemonState; // Assume enabled if running, will be corrected async
    refreshDaemonEnabledState();

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
    // (layoutChanged, layoutListChanged) that each cause a full D-Bus round trip.
    m_loadLayoutsTimer = new QTimer(this);
    m_loadLayoutsTimer->setSingleShot(true);
    m_loadLayoutsTimer->setInterval(50); // 50ms coalescing window
    connect(m_loadLayoutsTimer, &QTimer::timeout, this, &KCMPlasmaZones::loadLayouts);

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

    // Listen for screen layout assignment changes
    QDBusConnection::sessionBus().connect(
        QString(DBus::ServiceName), QString(DBus::ObjectPath), QString(DBus::Interface::LayoutManager),
        QStringLiteral("screenLayoutChanged"), this, SLOT(onScreenLayoutChanged(QString, QString)));


    // Listen for quick layout slot changes (e.g., when slots are modified externally)
    QDBusConnection::sessionBus().connect(
        QString(DBus::ServiceName), QString(DBus::ObjectPath), QString(DBus::Interface::LayoutManager),
        QStringLiteral("quickLayoutSlotsChanged"), this, SLOT(onQuickLayoutSlotsChanged()));

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
    return m_layouts;
}

// Layout to select getter
QString KCMPlasmaZones::layoutToSelect() const
{
    return m_layoutToSelect;
}

// Screens and assignments getters
QVariantList KCMPlasmaZones::screens() const
{
    return m_screens;
}
QVariantMap KCMPlasmaZones::screenAssignments() const
{
    return m_screenAssignments;
}

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
    const QVariantList converted = convertTriggersForStorage(triggers);
    if (m_settings->dragActivationTriggers() != converted) {
        m_settings->setDragActivationTriggers(converted);
        Q_EMIT dragActivationTriggersChanged();
        setNeedsSave(true);
    }
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
    if (!qFuzzyCompare(m_settings->activeOpacity(), opacity)) {
        m_settings->setActiveOpacity(opacity);
        Q_EMIT activeOpacityChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setInactiveOpacity(qreal opacity)
{
    if (!qFuzzyCompare(m_settings->inactiveOpacity(), opacity)) {
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
    if (m_settings->audioSpectrumBarCount() != count) {
        m_settings->setAudioSpectrumBarCount(count);
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
bool KCMPlasmaZones::autotileAnimationsEnabled() const
{
    return m_settings->autotileAnimationsEnabled();
}
int KCMPlasmaZones::autotileAnimationDuration() const
{
    return m_settings->autotileAnimationDuration();
}
bool KCMPlasmaZones::autotileFocusFollowsMouse() const
{
    return m_settings->autotileFocusFollowsMouse();
}
bool KCMPlasmaZones::autotileRespectMinimumSize() const
{
    return m_settings->autotileRespectMinimumSize();
}
bool KCMPlasmaZones::autotileShowActiveBorder() const
{
    return m_settings->autotileShowActiveBorder();
}
int KCMPlasmaZones::autotileActiveBorderWidth() const
{
    return m_settings->autotileActiveBorderWidth();
}
bool KCMPlasmaZones::autotileUseSystemBorderColor() const
{
    return m_settings->autotileUseSystemBorderColor();
}
QColor KCMPlasmaZones::autotileActiveBorderColor() const
{
    return m_settings->autotileActiveBorderColor();
}
bool KCMPlasmaZones::autotileMonocleHideOthers() const
{
    return m_settings->autotileMonocleHideOthers();
}
bool KCMPlasmaZones::autotileMonocleShowTabs() const
{
    return m_settings->autotileMonocleShowTabs();
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
        applyLayoutFilter();
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

void KCMPlasmaZones::setAutotileAnimationsEnabled(bool enabled)
{
    if (m_settings->autotileAnimationsEnabled() != enabled) {
        m_settings->setAutotileAnimationsEnabled(enabled);
        Q_EMIT autotileAnimationsEnabledChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setAutotileAnimationDuration(int duration)
{
    duration = qBound(50, duration, 500);
    if (m_settings->autotileAnimationDuration() != duration) {
        m_settings->setAutotileAnimationDuration(duration);
        Q_EMIT autotileAnimationDurationChanged();
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

void KCMPlasmaZones::setAutotileShowActiveBorder(bool show)
{
    if (m_settings->autotileShowActiveBorder() != show) {
        m_settings->setAutotileShowActiveBorder(show);
        Q_EMIT autotileShowActiveBorderChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setAutotileActiveBorderWidth(int width)
{
    width = qBound(1, width, 10);
    if (m_settings->autotileActiveBorderWidth() != width) {
        m_settings->setAutotileActiveBorderWidth(width);
        Q_EMIT autotileActiveBorderWidthChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setAutotileUseSystemBorderColor(bool use)
{
    if (m_settings->autotileUseSystemBorderColor() != use) {
        m_settings->setAutotileUseSystemBorderColor(use);
        Q_EMIT autotileUseSystemBorderColorChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setAutotileActiveBorderColor(const QColor &color)
{
    if (m_settings->autotileActiveBorderColor() != color) {
        m_settings->setAutotileActiveBorderColor(color);
        Q_EMIT autotileActiveBorderColorChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setAutotileMonocleHideOthers(bool hide)
{
    if (m_settings->autotileMonocleHideOthers() != hide) {
        m_settings->setAutotileMonocleHideOthers(hide);
        Q_EMIT autotileMonocleHideOthersChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setAutotileMonocleShowTabs(bool show)
{
    if (m_settings->autotileMonocleShowTabs() != show) {
        m_settings->setAutotileMonocleShowTabs(show);
        Q_EMIT autotileMonocleShowTabsChanged();
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

    m_settings->save();

    // Screen assignments and quick layout slots are owned by the daemon.
    // We only send changes via D-Bus; the daemon persists to assignments.json.

    QStringList failedOperations;
    const QString layoutInterface = QString(DBus::Interface::LayoutManager);

    // Apply screen assignments to daemon via D-Bus (batch - saves only once on daemon)
    // Convert m_screenAssignments to QVariantMap for D-Bus
    QVariantMap screenAssignments;
    for (auto it = m_screenAssignments.begin(); it != m_screenAssignments.end(); ++it) {
        screenAssignments[it.key()] = it.value().toString();
    }
    QDBusMessage screenReply = callDaemon(layoutInterface, QStringLiteral("setAllScreenAssignments"), {screenAssignments});
    if (screenReply.type() == QDBusMessage::ErrorMessage) {
        failedOperations.append(QStringLiteral("Screen assignments"));
    }

    // Apply quick layout slots to daemon via D-Bus (batch - saves only once on daemon)
    QVariantMap quickSlots;
    for (int slot = 1; slot <= 9; ++slot) {
        quickSlots[QString::number(slot)] = m_quickLayoutSlots.value(slot, QString());
    }
    QDBusMessage quickReply = callDaemon(layoutInterface, QStringLiteral("setAllQuickLayoutSlots"), {quickSlots});
    if (quickReply.type() == QDBusMessage::ErrorMessage) {
        failedOperations.append(QStringLiteral("Quick layout slots"));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Per-Desktop Assignments (batch)
    // ═══════════════════════════════════════════════════════════════════════════

    // Query current state, merge with pending changes, send as batch
    if (!m_pendingDesktopAssignments.isEmpty() || !m_clearedDesktopAssignments.isEmpty()) {
        QVariantMap desktopAssignments;

        // Get current state from daemon
        QDBusMessage currentReply = callDaemon(layoutInterface, QStringLiteral("getAllDesktopAssignments"));
        if (currentReply.type() == QDBusMessage::ReplyMessage && !currentReply.arguments().isEmpty()) {
            desktopAssignments = qdbus_cast<QVariantMap>(currentReply.arguments().first());
        }

        // Apply cleared assignments (remove from map)
        for (const QString& key : std::as_const(m_clearedDesktopAssignments)) {
            desktopAssignments.remove(key);
        }

        // Apply pending assignments (add/update in map)
        for (auto it = m_pendingDesktopAssignments.begin(); it != m_pendingDesktopAssignments.end(); ++it) {
            desktopAssignments[it.key()] = it.value();
        }

        // Send full state as batch
        QDBusMessage desktopReply = callDaemon(layoutInterface, QStringLiteral("setAllDesktopAssignments"), {desktopAssignments});
        if (desktopReply.type() == QDBusMessage::ErrorMessage) {
            failedOperations.append(QStringLiteral("Per-desktop assignments"));
        }

        m_clearedDesktopAssignments.clear();
        m_pendingDesktopAssignments.clear();
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Per-Activity Assignments (batch)
    // ═══════════════════════════════════════════════════════════════════════════

    // Query current state, merge with pending changes, send as batch
    if (!m_pendingActivityAssignments.isEmpty() || !m_clearedActivityAssignments.isEmpty()) {
        QVariantMap activityAssignments;

        // Get current state from daemon
        QDBusMessage currentReply = callDaemon(layoutInterface, QStringLiteral("getAllActivityAssignments"));
        if (currentReply.type() == QDBusMessage::ReplyMessage && !currentReply.arguments().isEmpty()) {
            activityAssignments = qdbus_cast<QVariantMap>(currentReply.arguments().first());
        }

        // Apply cleared assignments (remove from map)
        for (const QString& key : std::as_const(m_clearedActivityAssignments)) {
            activityAssignments.remove(key);
        }

        // Apply pending assignments (add/update in map)
        for (auto it = m_pendingActivityAssignments.begin(); it != m_pendingActivityAssignments.end(); ++it) {
            activityAssignments[it.key()] = it.value();
        }

        // Send full state as batch
        QDBusMessage activityReply = callDaemon(layoutInterface, QStringLiteral("setAllActivityAssignments"), {activityAssignments});
        if (activityReply.type() == QDBusMessage::ErrorMessage) {
            failedOperations.append(QStringLiteral("Per-activity assignments"));
        }

        m_clearedActivityAssignments.clear();
        m_pendingActivityAssignments.clear();
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Layout visibility (hiddenFromSelector)
    // ═══════════════════════════════════════════════════════════════════════════
    if (!m_pendingHiddenStates.isEmpty()) {
        for (auto it = m_pendingHiddenStates.cbegin(); it != m_pendingHiddenStates.cend(); ++it) {
            QDBusMessage hiddenReply = callDaemon(layoutInterface, QStringLiteral("setLayoutHidden"),
                                                  {it.key(), it.value()});
            if (hiddenReply.type() == QDBusMessage::ErrorMessage) {
                failedOperations.append(QStringLiteral("Layout visibility (%1)").arg(it.key()));
            }
        }
        m_pendingHiddenStates.clear();
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Layout auto-assign (autoAssign)
    // ═══════════════════════════════════════════════════════════════════════════
    if (!m_pendingAutoAssignStates.isEmpty()) {
        for (auto it = m_pendingAutoAssignStates.cbegin(); it != m_pendingAutoAssignStates.cend(); ++it) {
            QDBusMessage autoAssignReply = callDaemon(layoutInterface, QStringLiteral("setLayoutAutoAssign"),
                                                       {it.key(), it.value()});
            if (autoAssignReply.type() == QDBusMessage::ErrorMessage) {
                failedOperations.append(QStringLiteral("Layout auto-assign (%1)").arg(it.key()));
            }
        }
        m_pendingAutoAssignStates.clear();
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // App-to-zone rules (per-layout)
    // ═══════════════════════════════════════════════════════════════════════════
    if (!m_pendingAppRules.isEmpty()) {
        for (auto it = m_pendingAppRules.cbegin(); it != m_pendingAppRules.cend(); ++it) {
            const QString& layoutId = it.key();
            const QVariantList& rules = it.value();

            // Get current layout JSON from daemon
            QDBusMessage layoutReply = callDaemon(layoutInterface, QStringLiteral("getLayout"), {layoutId});
            if (layoutReply.type() != QDBusMessage::ReplyMessage || layoutReply.arguments().isEmpty()) {
                failedOperations.append(QStringLiteral("App rules (get %1)").arg(layoutId));
                continue;
            }

            QString json = layoutReply.arguments().first().toString();
            QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
            if (doc.isNull() || !doc.isObject()) {
                failedOperations.append(QStringLiteral("App rules (parse %1)").arg(layoutId));
                continue;
            }

            // Build appRules JSON array from pending rules
            QJsonArray rulesArray;
            for (const auto& ruleVar : rules) {
                QVariantMap ruleMap = ruleVar.toMap();
                PlasmaZones::AppRule rule;
                rule.pattern = ruleMap[QStringLiteral("pattern")].toString();
                rule.zoneNumber = ruleMap[QStringLiteral("zoneNumber")].toInt();
                rule.targetScreen = ruleMap[QStringLiteral("targetScreen")].toString();
                rulesArray.append(rule.toJson());
            }

            // Patch the layout JSON and send back
            QJsonObject obj = doc.object();
            obj[JsonKeys::AppRules] = rulesArray;
            QString updatedJson = QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
            QDBusMessage updateReply = callDaemon(layoutInterface, QStringLiteral("updateLayout"), {updatedJson});
            if (updateReply.type() == QDBusMessage::ErrorMessage) {
                failedOperations.append(QStringLiteral("App rules (save %1)").arg(layoutId));
            }
        }
        m_pendingAppRules.clear();
    }

    if (!failedOperations.isEmpty()) {
        qCWarning(lcKcm) << "Save: D-Bus operations failed:" << failedOperations.join(QStringLiteral(", "))
                         << "- some settings may not have been saved to daemon";
    }

    notifyDaemon();
    setNeedsSave(false);
    m_saveInProgress = false;
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
    Q_EMIT zoneSpanEnabledChanged();
    Q_EMIT snapAssistFeatureEnabledChanged();
    Q_EMIT snapAssistEnabledChanged();
    Q_EMIT snapAssistTriggersChanged();
    loadLayouts();
    refreshScreens();

    const QString layoutInterface = QString(DBus::Interface::LayoutManager);

    // Load screen assignments from daemon (single source of truth)
    // No fallback to local config - daemon owns this data via assignments.json
    m_screenAssignments.clear();
    QDBusMessage assignmentsReply = callDaemon(layoutInterface, QStringLiteral("getAllScreenAssignments"));
    if (assignmentsReply.type() == QDBusMessage::ReplyMessage && !assignmentsReply.arguments().isEmpty()) {
        QString assignmentsJson = assignmentsReply.arguments().first().toString();
        QJsonDocument doc = QJsonDocument::fromJson(assignmentsJson.toUtf8());
        if (doc.isObject()) {
            QJsonObject root = doc.object();
            // Structure: { "screenName": { "default": "layoutId", "1": "layoutId", ... } }
            for (auto it = root.begin(); it != root.end(); ++it) {
                QString screenName = it.key();
                QJsonObject screenObj = it.value().toObject();
                // Use the "default" (all desktops) assignment for the simple screen assignment
                if (screenObj.contains(QStringLiteral("default"))) {
                    QString layoutId = screenObj.value(QStringLiteral("default")).toString();
                    if (!layoutId.isEmpty()) {
                        m_screenAssignments[screenName] = layoutId;
                    }
                }
            }
        }
    }
    // If daemon not available, m_screenAssignments stays empty (correct behavior)

    // Load quick layout slots from daemon (single source of truth)
    // No fallback to local config - daemon owns this data via assignments.json
    m_quickLayoutSlots.clear();
    QDBusMessage slotsReply = callDaemon(layoutInterface, QStringLiteral("getAllQuickLayoutSlots"));
    if (slotsReply.type() == QDBusMessage::ReplyMessage && !slotsReply.arguments().isEmpty()) {
        QVariantMap slotsMap = qdbus_cast<QVariantMap>(slotsReply.arguments().first());
        for (auto it = slotsMap.begin(); it != slotsMap.end(); ++it) {
            bool ok;
            int slotNum = it.key().toInt(&ok);
            if (ok && slotNum >= 1 && slotNum <= 9) {
                QString layoutId = it.value().toString();
                if (!layoutId.isEmpty()) {
                    m_quickLayoutSlots[slotNum] = layoutId;
                }
            }
        }
    }
    // If daemon not available, m_quickLayoutSlots stays empty (correct behavior)

    // Clear pending per-desktop and per-activity assignments (discard unsaved changes)
    m_pendingDesktopAssignments.clear();
    m_clearedDesktopAssignments.clear();
    m_pendingActivityAssignments.clear();
    m_clearedActivityAssignments.clear();

    // Clear pending layout visibility changes (discard unsaved changes)
    m_pendingHiddenStates.clear();

    // Clear pending auto-assign changes (discard unsaved changes)
    m_pendingAutoAssignStates.clear();

    // Clear pending app rules (discard unsaved changes)
    m_pendingAppRules.clear();

    Q_EMIT screenAssignmentsChanged();
    Q_EMIT activityAssignmentsChanged();
    Q_EMIT appRulesRefreshed();
    setNeedsSave(false);
}

void KCMPlasmaZones::defaults()
{
    m_settings->reset();

    // Set default layout: pick the layout with the lowest defaultOrder (Columns (2) has 0)
    int bestOrder = 999;
    QString bestId;
    for (const QVariant& layoutVar : m_layouts) {
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

    // Clear screen assignments
    m_screenAssignments.clear();

    // Clear quick layout slots
    m_quickLayoutSlots.clear();

    // Clear pending per-desktop and per-activity assignments
    m_pendingDesktopAssignments.clear();
    m_clearedDesktopAssignments.clear();
    m_pendingActivityAssignments.clear();
    m_clearedActivityAssignments.clear();

    // Reset all layouts to visible (clear hidden states)
    m_pendingHiddenStates.clear();

    // Reset all layouts to manual (clear auto-assign states)
    m_pendingAutoAssignStates.clear();

    // Stage empty app rules for all layouts (clears any daemon-side rules on Apply)
    m_pendingAppRules.clear();
    for (int i = 0; i < m_layouts.size(); ++i) {
        QVariantMap layout = m_layouts[i].toMap();
        QString layoutId = layout[QStringLiteral("id")].toString();
        if (!layoutId.isEmpty()) {
            m_pendingAppRules[layoutId] = QVariantList();
        }
    }
    Q_EMIT appRulesRefreshed();
    for (int i = 0; i < m_layouts.size(); ++i) {
        QVariantMap layout = m_layouts[i].toMap();
        bool changed = false;
        if (layout[QStringLiteral("hiddenFromSelector")].toBool()) {
            layout[QStringLiteral("hiddenFromSelector")] = false;
            m_pendingHiddenStates[layout[QStringLiteral("id")].toString()] = false;
            changed = true;
        }
        if (layout[QStringLiteral("autoAssign")].toBool()) {
            layout[QStringLiteral("autoAssign")] = false;
            m_pendingAutoAssignStates[layout[QStringLiteral("id")].toString()] = false;
            changed = true;
        }
        if (changed) {
            m_layouts[i] = layout;
        }
    }
    Q_EMIT layoutsChanged();

    // Emit all property change signals so UI updates
    Q_EMIT screenAssignmentsChanged();
    Q_EMIT activityAssignmentsChanged();
    Q_EMIT dragActivationTriggersChanged();
    Q_EMIT zoneSpanEnabledChanged();
    Q_EMIT zoneSpanTriggersChanged();
    Q_EMIT toggleActivationChanged();
    Q_EMIT showZonesOnAllMonitorsChanged();
    Q_EMIT disabledMonitorsChanged();
    Q_EMIT showZoneNumbersChanged();
    Q_EMIT flashZonesOnSwitchChanged();
    Q_EMIT showOsdOnLayoutSwitchChanged();
    Q_EMIT showNavigationOsdChanged();
    Q_EMIT osdStyleChanged();
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
    Q_EMIT zonePaddingChanged();
    Q_EMIT outerGapChanged();
    Q_EMIT adjacentThresholdChanged();
    Q_EMIT keepWindowsInZonesOnResolutionChangeChanged();
    Q_EMIT moveNewWindowsToLastZoneChanged();
    Q_EMIT restoreOriginalSizeOnUnsnapChanged();
    Q_EMIT stickyWindowHandlingChanged();
    Q_EMIT restoreWindowsToZonesOnLoginChanged();
    Q_EMIT snapAssistFeatureEnabledChanged();
    Q_EMIT snapAssistEnabledChanged();
    Q_EMIT snapAssistTriggersChanged();
    Q_EMIT defaultLayoutIdChanged();
    Q_EMIT excludedApplicationsChanged();
    Q_EMIT excludedWindowClassesChanged();
    Q_EMIT excludeTransientWindowsChanged();
    Q_EMIT minimumWindowWidthChanged();
    Q_EMIT minimumWindowHeightChanged();
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
    Q_EMIT autotileAnimationsEnabledChanged();
    Q_EMIT autotileAnimationDurationChanged();
    Q_EMIT autotileFocusFollowsMouseChanged();
    Q_EMIT autotileRespectMinimumSizeChanged();
    Q_EMIT autotileShowActiveBorderChanged();
    Q_EMIT autotileActiveBorderWidthChanged();
    Q_EMIT autotileUseSystemBorderColorChanged();
    Q_EMIT autotileActiveBorderColorChanged();
    Q_EMIT autotileMonocleHideOthersChanged();
    Q_EMIT autotileMonocleShowTabsChanged();

    // Reset editor shortcuts to defaults
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
    KConfigGroup editorGroup = config->group(QStringLiteral("Editor"));
    editorGroup.deleteGroup(); // Delete editor group to use defaults

    // Clean up legacy config groups (no longer used - daemon owns this data)
    KConfigGroup assignmentsGroup = config->group(QStringLiteral("ScreenAssignments"));
    assignmentsGroup.deleteGroup();
    KConfigGroup slotsGroup = config->group(QStringLiteral("QuickLayoutSlots"));
    slotsGroup.deleteGroup();

    config->sync();

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

void KCMPlasmaZones::createNewLayout()
{
    QDBusMessage reply = callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("createLayout"),
                                    {QStringLiteral("New Layout"), QStringLiteral("grid")});

    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        QString newLayoutId = reply.arguments().first().toString();
        if (!newLayoutId.isEmpty()) {
            m_layoutToSelect = newLayoutId;
            // Don't emit here - wait for loadLayouts() to complete first
        }
    }
    // The daemon emits layoutListChanged after creation, which triggers scheduleLoadLayouts()
}

void KCMPlasmaZones::deleteLayout(const QString& layoutId)
{
    QDBusMessage msg =
        QDBusMessage::createMethodCall(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                       QString(DBus::Interface::LayoutManager), QStringLiteral("deleteLayout"));
    msg << layoutId;
    watchAsyncDbusCall(QDBusConnection::sessionBus().asyncCall(msg), QStringLiteral("deleteLayout"));
    // The daemon emits layoutListChanged after deletion, which triggers scheduleLoadLayouts()
}

void KCMPlasmaZones::duplicateLayout(const QString& layoutId)
{
    QDBusMessage reply =
        callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("duplicateLayout"), {layoutId});

    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        QString newLayoutId = reply.arguments().first().toString();
        if (!newLayoutId.isEmpty()) {
            m_layoutToSelect = newLayoutId;
            // Don't emit here - wait for loadLayouts() to complete first
        }
    }
    // The daemon emits layoutListChanged after duplication, which triggers scheduleLoadLayouts()
}

void KCMPlasmaZones::importLayout(const QString& filePath)
{
    QDBusMessage reply =
        callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("importLayout"), {filePath});

    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        QString newLayoutId = reply.arguments().first().toString();
        if (!newLayoutId.isEmpty()) {
            m_layoutToSelect = newLayoutId;
            // Don't emit here - wait for loadLayouts() to complete first
        }
    }
    // The daemon emits layoutListChanged after import, which triggers scheduleLoadLayouts()
}

void KCMPlasmaZones::exportLayout(const QString& layoutId, const QString& filePath)
{
    QDBusMessage msg =
        QDBusMessage::createMethodCall(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                       QString(DBus::Interface::LayoutManager), QStringLiteral("exportLayout"));
    msg << layoutId << filePath;
    watchAsyncDbusCall(QDBusConnection::sessionBus().asyncCall(msg), QStringLiteral("exportLayout"));
}

void KCMPlasmaZones::editLayout(const QString& layoutId)
{
    QString screenName = currentScreenName();

    QDBusMessage msg = QDBusMessage::createMethodCall(
        QString(DBus::ServiceName), QString(DBus::ObjectPath),
        QString(DBus::Interface::LayoutManager), QStringLiteral("openEditorForLayoutOnScreen"));
    msg << layoutId << screenName;
    watchAsyncDbusCall(QDBusConnection::sessionBus().asyncCall(msg), QStringLiteral("editLayout"));
}

void KCMPlasmaZones::openEditor()
{
    QString screenName = currentScreenName();

    QDBusMessage msg;
    if (!screenName.isEmpty()) {
        msg = QDBusMessage::createMethodCall(
            QString(DBus::ServiceName), QString(DBus::ObjectPath),
            QString(DBus::Interface::LayoutManager), QStringLiteral("openEditorForScreen"));
        msg << screenName;
    } else {
        msg = QDBusMessage::createMethodCall(
            QString(DBus::ServiceName), QString(DBus::ObjectPath),
            QString(DBus::Interface::LayoutManager), QStringLiteral("openEditor"));
    }
    watchAsyncDbusCall(QDBusConnection::sessionBus().asyncCall(msg), QStringLiteral("openEditor"));
}

void KCMPlasmaZones::setLayoutHidden(const QString& layoutId, bool hidden)
{
    // Stage the change locally (applied on save)
    m_pendingHiddenStates[layoutId] = hidden;

    // Update local model so the UI reflects the change immediately
    for (int i = 0; i < m_layouts.size(); ++i) {
        QVariantMap layout = m_layouts[i].toMap();
        if (layout[QStringLiteral("id")].toString() == layoutId) {
            layout[QStringLiteral("hiddenFromSelector")] = hidden;
            m_layouts[i] = layout;
            break;
        }
    }
    Q_EMIT layoutsChanged();

    setNeedsSave(true);
}

void KCMPlasmaZones::setLayoutAutoAssign(const QString& layoutId, bool enabled)
{
    // Stage the change locally (applied on save)
    m_pendingAutoAssignStates[layoutId] = enabled;

    // Update local model so the UI reflects the change immediately
    for (int i = 0; i < m_layouts.size(); ++i) {
        QVariantMap layout = m_layouts[i].toMap();
        if (layout[QStringLiteral("id")].toString() == layoutId) {
            layout[QStringLiteral("autoAssign")] = enabled;
            m_layouts[i] = layout;
            break;
        }
    }
    Q_EMIT layoutsChanged();

    setNeedsSave(true);
}

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

// Daemon status methods
bool KCMPlasmaZones::isDaemonRunning() const
{
    return QDBusConnection::sessionBus().interface()->isServiceRegistered(QString(DBus::ServiceName));
}

bool KCMPlasmaZones::isDaemonEnabled() const
{
    return m_daemonEnabled;
}

void KCMPlasmaZones::setDaemonEnabled(bool enabled)
{
    if (m_daemonEnabled == enabled) {
        return;
    }
    m_daemonEnabled = enabled;

    // Update systemd service enabled state
    setDaemonAutostart(enabled);

    // Start or stop daemon immediately
    if (enabled) {
        startDaemon();
    } else {
        stopDaemon();
    }

    Q_EMIT daemonEnabledChanged();
}

void KCMPlasmaZones::runSystemctl(const QStringList& args, SystemctlCallback callback)
{
    auto* proc = new QProcess(this);
    connect(proc, &QProcess::finished, this, [proc, callback, args](int exitCode, QProcess::ExitStatus status) {
        bool success = (status == QProcess::NormalExit && exitCode == 0);
        QString output = QString::fromUtf8(proc->readAllStandardOutput()).trimmed();
        if (!success) {
            QString errorOutput = QString::fromUtf8(proc->readAllStandardError()).trimmed();
            qCWarning(lcKcm) << "systemctl" << args << "failed:" << errorOutput;
        }
        if (callback) {
            callback(success, output);
        }
        proc->deleteLater();
    });
    proc->start(QStringLiteral("systemctl"), args);
}

void KCMPlasmaZones::startDaemon()
{
    if (isDaemonRunning()) {
        return;
    }
    runSystemctl({QStringLiteral("--user"), QStringLiteral("start"), QLatin1String(KCMConstants::SystemdServiceName)});
    // Layouts will be loaded when daemonReady D-Bus signal is received
}

void KCMPlasmaZones::stopDaemon()
{
    if (!isDaemonRunning()) {
        return;
    }
    runSystemctl({QStringLiteral("--user"), QStringLiteral("stop"), QLatin1String(KCMConstants::SystemdServiceName)});
}

void KCMPlasmaZones::refreshDaemonEnabledState()
{
    runSystemctl(
        {QStringLiteral("--user"), QStringLiteral("is-enabled"), QLatin1String(KCMConstants::SystemdServiceName)},
        [this](bool /*success*/, const QString& output) {
            bool enabled = (output == QLatin1String("enabled"));
            if (m_daemonEnabled != enabled) {
                m_daemonEnabled = enabled;
                Q_EMIT daemonEnabledChanged();
            }
        });
}

void KCMPlasmaZones::setDaemonAutostart(bool enabled)
{
    QString action = enabled ? QStringLiteral("enable") : QStringLiteral("disable");
    runSystemctl({QStringLiteral("--user"), action, QLatin1String(KCMConstants::SystemdServiceName)},
                 [this](bool success, const QString& /*output*/) {
                     if (success) {
                         // Refresh the enabled state to confirm the change
                         refreshDaemonEnabledState();
                     }
                 });
}

void KCMPlasmaZones::checkDaemonStatus()
{
    bool currentState = isDaemonRunning();
    if (currentState != m_lastDaemonState) {
        m_lastDaemonState = currentState;
        Q_EMIT daemonRunningChanged();

        // Note: When daemon starts, we rely on the daemonReady D-Bus signal
        // to trigger loadLayouts(). No polling needed here.
    }
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
    if (m_saveInProgress) return; // Don't reload layouts while a save is in progress
    // Restart the debounce timer — coalesces rapid signals into one loadLayouts() call
    m_loadLayoutsTimer->start();
}

void KCMPlasmaZones::loadLayouts()
{
    QVariantList newLayouts;

    // Load from daemon via D-Bus
    QDBusMessage reply = callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("getLayoutList"));

    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        QStringList layoutJsonList = reply.arguments().first().toStringList();
        for (const QString& layoutJson : layoutJsonList) {
            QJsonDocument doc = QJsonDocument::fromJson(layoutJson.toUtf8());
            if (!doc.isNull() && doc.isObject()) {
                newLayouts.append(doc.object().toVariantMap());
            }
        }
    }

    // No fallback layouts - if daemon isn't running, show empty list
    // The QML UI should handle this gracefully with a message like "Enable daemon to see layouts"

    m_unfilteredLayouts = newLayouts;
    applyLayoutFilter();
}

void KCMPlasmaZones::applyLayoutFilter()
{
    QVariantList newLayouts = m_unfilteredLayouts;

    // Filter out autotile entries when the feature is disabled
    if (!m_settings->autotileEnabled()) {
        newLayouts.erase(
            std::remove_if(newLayouts.begin(), newLayouts.end(), [](const QVariant& v) {
                return v.toMap().value(QStringLiteral("isAutotile")).toBool();
            }),
            newLayouts.end());
    }

    // Sort: manual layouts first (alphabetical), then autotile (alphabetical)
    // Matches the zone selector popup ordering
    std::sort(newLayouts.begin(), newLayouts.end(), [](const QVariant& a, const QVariant& b) {
        const QVariantMap mapA = a.toMap();
        const QVariantMap mapB = b.toMap();
        const bool aIsAutotile = mapA.value(QStringLiteral("isAutotile")).toBool();
        const bool bIsAutotile = mapB.value(QStringLiteral("isAutotile")).toBool();
        if (aIsAutotile != bIsAutotile) {
            return !aIsAutotile; // manual before autotile
        }
        return mapA.value(QStringLiteral("name")).toString().toLower()
             < mapB.value(QStringLiteral("name")).toString().toLower();
    });

    // Only update if the list actually changed (prevents scroll position reset).
    // Compare by ID list — QVariantMap equality is unreliable across D-Bus round-trips
    // due to type coercion differences (int vs double, etc.).
    auto extractIds = [](const QVariantList& list) {
        QStringList ids;
        ids.reserve(list.size());
        for (const auto& v : list) {
            ids.append(v.toMap().value(QStringLiteral("id")).toString());
        }
        return ids;
    };
    if (extractIds(m_layouts) == extractIds(newLayouts)) {
        return;
    }

    // Track whether layoutToSelect was set externally (create/import/duplicate)
    // before we emit layoutsChanged, so we know to emit layoutToSelectChanged later.
    const bool hasExternalSelect = !m_layoutToSelect.isEmpty();

    m_layouts = newLayouts;
    Q_EMIT layoutsChanged();

    // Only auto-select the default layout on the very first successful load.
    // On refreshes (D-Bus signals, autotile toggle), preserve the user's scroll position.
    if (!m_initialLayoutLoadDone && !newLayouts.isEmpty()) {
        m_initialLayoutLoadDone = true;

        QString defaultId = defaultLayoutId();
        if (defaultId.isEmpty()) {
            // No explicit default set (fresh install / reset) — resolve the implicit
            // default by defaultOrder (lowest wins, matching daemon's LayoutManager logic).
            // This ensures the star badge appears on Columns (2) out of the box.
            int bestOrder = 999;
            for (const QVariant& v : newLayouts) {
                const QVariantMap layoutMap = v.toMap();
                int order = layoutMap.value(QStringLiteral("defaultOrder"), 999).toInt();
                if (order < bestOrder) {
                    bestOrder = order;
                    defaultId = layoutMap.value(QStringLiteral("id")).toString();
                }
            }
            if (!defaultId.isEmpty()) {
                m_settings->setDefaultLayoutId(defaultId);
                Q_EMIT defaultLayoutIdChanged();
            }
        }
        if (!defaultId.isEmpty()) {
            m_layoutToSelect = defaultId;
        }
    }

    // Emit layoutToSelectChanged for initial load or externally-triggered selection
    // (create/import/duplicate set m_layoutToSelect before calling loadLayouts).
    // For plain refreshes neither branch is true, so scroll position is preserved.
    if (hasExternalSelect || !m_layoutToSelect.isEmpty()) {
        if (!m_layoutToSelect.isEmpty()) {
            Q_EMIT layoutToSelectChanged();
            // Clear after emission — one-shot value. QML signal handlers capture
            // the value synchronously before any deferred Qt.callLater callbacks.
            m_layoutToSelect.clear();
        }
    }
}

void KCMPlasmaZones::onScreenLayoutChanged(const QString& screenName, const QString& layoutId)
{
    // When screen layout assignment changes externally, update our local state.
    // The signal carries a screen ID (EDID-based) from LayoutManager, but our
    // local m_screenAssignments cache is keyed by connector names. Resolve back.
    if (screenName.isEmpty()) {
        return;
    }

    QString connectorName;
    QScreen* screen = Utils::findScreenByIdOrName(screenName);
    if (screen) {
        connectorName = screen->name();
    } else {
        // Screen not connected — fall back to original value
        connectorName = screenName;
    }

    if (layoutId.isEmpty()) {
        m_screenAssignments.remove(connectorName);
    } else {
        m_screenAssignments[connectorName] = layoutId;
    }

    Q_EMIT screenAssignmentsChanged();

    // Also refresh screens to update any screen-related UI
    refreshScreens();
}

void KCMPlasmaZones::onQuickLayoutSlotsChanged()
{
    // When quick layout slots change externally, reload them from daemon asynchronously
    // Using asyncCall to avoid blocking the UI thread
    QDBusMessage msg = QDBusMessage::createMethodCall(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                                      QString(DBus::Interface::LayoutManager),
                                                      QStringLiteral("getAllQuickLayoutSlots"));

    QDBusPendingCall pendingCall = QDBusConnection::sessionBus().asyncCall(msg);
    QDBusPendingCallWatcher* watcher = new QDBusPendingCallWatcher(pendingCall, this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* watcher) {
        watcher->deleteLater();

        QDBusPendingReply<QVariantMap> reply = *watcher;
        if (reply.isError()) {
            qCWarning(lcKcm) << "Failed to get quick layout slots:" << reply.error().message();
            return;
        }

        m_quickLayoutSlots.clear();
        QVariantMap slots = reply.value();
        for (auto it = slots.begin(); it != slots.end(); ++it) {
            bool ok;
            int slotNum = it.key().toInt(&ok);
            if (ok && slotNum >= 1 && slotNum <= 9) {
                QString layoutId = it.value().toString();
                if (!layoutId.isEmpty()) {
                    m_quickLayoutSlots[slotNum] = layoutId;
                }
            }
        }

    });
}

void KCMPlasmaZones::onSettingsChanged()
{
    // When settings change externally (e.g., via D-Bus from another process),
    // reload settings from the config file
    if (m_settings) {
        m_settings->load();

        // Emit signals for all properties that might have changed. Not tracking which
        // ones actually changed since external changes are rare, signal emission is cheap,
        // and QML only updates when values differ.
        Q_EMIT dragActivationTriggersChanged();
        Q_EMIT zoneSpanEnabledChanged();
        Q_EMIT zoneSpanTriggersChanged();
        Q_EMIT toggleActivationChanged();
        Q_EMIT showZonesOnAllMonitorsChanged();
        Q_EMIT disabledMonitorsChanged();
        Q_EMIT showZoneNumbersChanged();
        Q_EMIT flashZonesOnSwitchChanged();
        Q_EMIT showOsdOnLayoutSwitchChanged();
        Q_EMIT showNavigationOsdChanged();
        Q_EMIT osdStyleChanged();
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
        Q_EMIT zonePaddingChanged();
        Q_EMIT outerGapChanged();
        Q_EMIT adjacentThresholdChanged();
        Q_EMIT keepWindowsInZonesOnResolutionChangeChanged();
        Q_EMIT moveNewWindowsToLastZoneChanged();
        Q_EMIT restoreOriginalSizeOnUnsnapChanged();
        Q_EMIT stickyWindowHandlingChanged();
        Q_EMIT restoreWindowsToZonesOnLoginChanged();
        Q_EMIT snapAssistFeatureEnabledChanged();
        Q_EMIT snapAssistEnabledChanged();
        Q_EMIT snapAssistTriggersChanged();
        Q_EMIT defaultLayoutIdChanged();
        Q_EMIT excludedApplicationsChanged();
        Q_EMIT excludedWindowClassesChanged();
        Q_EMIT excludeTransientWindowsChanged();
        Q_EMIT minimumWindowWidthChanged();
        Q_EMIT minimumWindowHeightChanged();
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
        Q_EMIT autotileAnimationsEnabledChanged();
        Q_EMIT autotileAnimationDurationChanged();
        Q_EMIT autotileFocusFollowsMouseChanged();
        Q_EMIT autotileRespectMinimumSizeChanged();
        Q_EMIT autotileShowActiveBorderChanged();
        Q_EMIT autotileActiveBorderWidthChanged();
        Q_EMIT autotileUseSystemBorderColorChanged();
        Q_EMIT autotileActiveBorderColorChanged();
        Q_EMIT autotileMonocleHideOthersChanged();
        Q_EMIT autotileMonocleShowTabsChanged();
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
    // Notify daemon to reload settings via the SettingsAdaptor interface
    QDBusMessage msg =
        QDBusMessage::createMethodCall(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                       QString(DBus::Interface::Settings), QStringLiteral("reloadSettings"));
    QDBusConnection::sessionBus().asyncCall(msg);
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
    if (screenName.isEmpty() || activityId.isEmpty()) {
        qCWarning(lcKcm) << "Cannot assign layout - empty screen name or activity ID";
        return;
    }

    // Use screenId|activity format to match daemon's getAllActivityAssignments() key format
    QString key = QStringLiteral("%1|%2").arg(Utils::screenIdForName(screenName), activityId);

    // Track this assignment in pending cache
    if (layoutId.isEmpty()) {
        // Clearing assignment
        m_pendingActivityAssignments.remove(key);
        m_clearedActivityAssignments.insert(key);
    } else {
        m_pendingActivityAssignments[key] = layoutId;
        m_clearedActivityAssignments.remove(key);
    }

    Q_EMIT activityAssignmentsChanged();
    Q_EMIT screenAssignmentsChanged();
    setNeedsSave(true);
}

void KCMPlasmaZones::clearScreenActivityAssignment(const QString& screenName, const QString& activityId)
{
    assignLayoutToScreenActivity(screenName, activityId, QString());
}

QString KCMPlasmaZones::getLayoutForScreenActivity(const QString& screenName, const QString& activityId) const
{
    // Check pending cache first (unsaved changes)
    // Use screenId|activity format to match daemon's key format
    QString key = QStringLiteral("%1|%2").arg(Utils::screenIdForName(screenName), activityId);
    if (m_pendingActivityAssignments.contains(key)) {
        return m_pendingActivityAssignments.value(key);
    }

    // Check if explicitly cleared
    if (m_clearedActivityAssignments.contains(key)) {
        return QString();
    }

    // Query daemon for the layout assigned to this screen/activity combination
    QDBusMessage reply = callDaemon(QString(DBus::Interface::LayoutManager),
                                    QStringLiteral("getLayoutForScreenActivity"), {screenName, activityId});

    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        return reply.arguments().first().toString();
    }

    return QString();
}

bool KCMPlasmaZones::hasExplicitAssignmentForScreenActivity(const QString& screenName, const QString& activityId) const
{
    // Check pending cache first
    // Use screenId|activity format to match daemon's key format
    QString key = QStringLiteral("%1|%2").arg(Utils::screenIdForName(screenName), activityId);
    if (m_pendingActivityAssignments.contains(key)) {
        return true;
    }
    if (m_clearedActivityAssignments.contains(key)) {
        return false;
    }

    // Query daemon
    QDBusMessage reply = callDaemon(QString(DBus::Interface::LayoutManager),
                                    QStringLiteral("hasExplicitAssignmentForScreenActivity"), {screenName, activityId});

    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        return reply.arguments().first().toBool();
    }

    return false;
}

void KCMPlasmaZones::assignLayoutToScreen(const QString& screenName, const QString& layoutId)
{
    // Store the assignment locally - it will be persisted on save()
    QString oldLayoutId = m_screenAssignments.value(screenName).toString();
    if (oldLayoutId != layoutId) {
        if (layoutId.isEmpty()) {
            m_screenAssignments.remove(screenName);
        } else {
            m_screenAssignments[screenName] = layoutId;
        }
        Q_EMIT screenAssignmentsChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::clearScreenAssignment(const QString& screenName)
{
    // Store locally - it will be persisted on save()
    if (m_screenAssignments.contains(screenName)) {
        m_screenAssignments.remove(screenName);
        Q_EMIT screenAssignmentsChanged();
        setNeedsSave(true);
    }
}

QString KCMPlasmaZones::getLayoutForScreen(const QString& screenName) const
{
    return m_screenAssignments.value(screenName).toString();
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

// Per-screen zone selector settings
QVariantMap KCMPlasmaZones::getPerScreenZoneSelectorSettings(const QString& screenName) const
{
    return m_settings ? m_settings->getPerScreenZoneSelectorSettings(Utils::screenIdForName(screenName)) : QVariantMap();
}

void KCMPlasmaZones::setPerScreenZoneSelectorSetting(const QString& screenName, const QString& key, const QVariant& value)
{
    if (!m_settings || screenName.isEmpty()) {
        return;
    }
    // Translate connector name to stable EDID-based screen ID for storage
    m_settings->setPerScreenZoneSelectorSetting(Utils::screenIdForName(screenName), key, value);
    setNeedsSave(true);
}

void KCMPlasmaZones::clearPerScreenZoneSelectorSettings(const QString& screenName)
{
    if (!m_settings || screenName.isEmpty()) {
        return;
    }
    m_settings->clearPerScreenZoneSelectorSettings(Utils::screenIdForName(screenName));
    setNeedsSave(true);
}

bool KCMPlasmaZones::hasPerScreenZoneSelectorSettings(const QString& screenName) const
{
    return m_settings ? m_settings->hasPerScreenZoneSelectorSettings(Utils::screenIdForName(screenName)) : false;
}

void KCMPlasmaZones::assignLayoutToScreenDesktop(const QString& screenName, int virtualDesktop, const QString& layoutId)
{
    if (screenName.isEmpty()) {
        qCWarning(lcKcm) << "Cannot assign layout - empty screen name";
        return;
    }

    // Cache the assignment locally - will be sent to daemon on Apply
    // Use screenId|desktop format to match daemon's getAllDesktopAssignments() key format
    QString screenId = Utils::screenIdForName(screenName);
    QString key = QStringLiteral("%1|%2").arg(screenId).arg(virtualDesktop);

    if (layoutId.isEmpty()) {
        // Empty layoutId means clear - but we handle that in clearScreenDesktopAssignment
        m_pendingDesktopAssignments.remove(key);
        m_clearedDesktopAssignments.insert(key);
    } else {
        m_pendingDesktopAssignments[key] = layoutId;
        m_clearedDesktopAssignments.remove(key);
    }

    // If this is for "all desktops" (virtualDesktop=0), also update local display cache
    if (virtualDesktop == 0) {
        if (layoutId.isEmpty()) {
            m_screenAssignments.remove(screenName);
        } else {
            m_screenAssignments[screenName] = layoutId;
        }
    }

    Q_EMIT screenAssignmentsChanged();
    setNeedsSave(true);
}

void KCMPlasmaZones::clearScreenDesktopAssignment(const QString& screenName, int virtualDesktop)
{
    if (screenName.isEmpty()) {
        qCWarning(lcKcm) << "Cannot clear assignment - empty screen name";
        return;
    }

    // Cache the clear locally - will be sent to daemon on Apply
    // Use screenId|desktop format to match daemon's getAllDesktopAssignments() key format
    QString screenId = Utils::screenIdForName(screenName);
    QString key = QStringLiteral("%1|%2").arg(screenId).arg(virtualDesktop);
    m_pendingDesktopAssignments.remove(key);
    m_clearedDesktopAssignments.insert(key);

    // If this is for "all desktops" (virtualDesktop=0), also update local display cache
    if (virtualDesktop == 0 && m_screenAssignments.contains(screenName)) {
        m_screenAssignments.remove(screenName);
    }

    Q_EMIT screenAssignmentsChanged();
    setNeedsSave(true);
}

QString KCMPlasmaZones::getLayoutForScreenDesktop(const QString& screenName, int virtualDesktop) const
{
    // Check pending cache first (unsaved changes)
    // Use screenId|desktop format to match daemon's key format
    QString key = QStringLiteral("%1|%2").arg(Utils::screenIdForName(screenName)).arg(virtualDesktop);
    if (m_pendingDesktopAssignments.contains(key)) {
        return m_pendingDesktopAssignments.value(key);
    }
    // Check if it was cleared but not yet saved
    if (m_clearedDesktopAssignments.contains(key)) {
        return QString();
    }

    // Query daemon for the layout assigned to this screen/desktop combination
    QDBusMessage reply = callDaemon(QString(DBus::Interface::LayoutManager),
                                    QStringLiteral("getLayoutForScreenDesktop"), {screenName, virtualDesktop});

    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        return reply.arguments().first().toString();
    }

    return QString();
}

bool KCMPlasmaZones::hasExplicitAssignmentForScreenDesktop(const QString& screenName, int virtualDesktop) const
{
    // Check pending cache first (unsaved changes)
    // Use screenId|desktop format to match daemon's key format
    QString key = QStringLiteral("%1|%2").arg(Utils::screenIdForName(screenName)).arg(virtualDesktop);
    if (m_pendingDesktopAssignments.contains(key)) {
        return true; // Has pending assignment
    }
    // Check if it was cleared but not yet saved
    if (m_clearedDesktopAssignments.contains(key)) {
        return false; // Pending clear means no explicit assignment
    }

    // Query daemon for whether there's an explicit assignment (not inherited from fallback)
    QDBusMessage reply =
        callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("hasExplicitAssignmentForScreenDesktop"),
                   {screenName, virtualDesktop});

    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        return reply.arguments().first().toBool();
    }

    return false;
}

QString KCMPlasmaZones::getQuickLayoutSlot(int slotNumber) const
{
    if (slotNumber < 1 || slotNumber > 9) {
        return QString();
    }
    return m_quickLayoutSlots.value(slotNumber, QString());
}

void KCMPlasmaZones::setQuickLayoutSlot(int slotNumber, const QString& layoutId)
{
    if (slotNumber < 1 || slotNumber > 9) {
        return;
    }

    // Store locally - it will be persisted on save()
    QString oldLayoutId = m_quickLayoutSlots.value(slotNumber, QString());
    if (oldLayoutId != layoutId) {
        if (layoutId.isEmpty()) {
            m_quickLayoutSlots.remove(slotNumber);
        } else {
            m_quickLayoutSlots[slotNumber] = layoutId;
        }
        setNeedsSave(true);
    }
}

QString KCMPlasmaZones::getQuickLayoutShortcut(int slotNumber) const
{
    if (slotNumber < 1 || slotNumber > 9) {
        return QString();
    }

    // Query KGlobalAccel for the actual registered shortcut
    // This reflects what's actually in the system, including user changes via System Settings
    const QString componentName = QStringLiteral("plasmazonesd");
    const QString actionId = QStringLiteral("quick_layout_%1").arg(slotNumber);
    QList<QKeySequence> shortcuts = KGlobalAccel::self()->globalShortcut(componentName, actionId);

    if (!shortcuts.isEmpty() && !shortcuts.first().isEmpty()) {
        return shortcuts.first().toString(QKeySequence::NativeText);
    }

    // If KGlobalAccel returns empty, the shortcut is not assigned
    // Don't fall back to settings defaults as that would be misleading
    return QString();
}

// ═══════════════════════════════════════════════════════════════════════════════
// App-to-Zone Rules
// ═══════════════════════════════════════════════════════════════════════════════

QVariantList KCMPlasmaZones::getAppRulesForLayout(const QString& layoutId) const
{
    // Check pending cache first (unsaved changes)
    if (m_pendingAppRules.contains(layoutId)) {
        return m_pendingAppRules.value(layoutId);
    }

    // Fall back to daemon
    QDBusMessage reply =
        callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("getLayout"), {layoutId});

    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return {};
    }

    QString json = reply.arguments().first().toString();
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (doc.isNull() || !doc.isObject()) {
        return {};
    }

    QJsonArray rulesArray = doc.object()[QLatin1String("appRules")].toArray();
    QVariantList result;
    for (const auto& ruleVal : rulesArray) {
        QJsonObject ruleObj = ruleVal.toObject();
        QVariantMap rule;
        rule[QStringLiteral("pattern")] = ruleObj[QLatin1String("pattern")].toString();
        rule[QStringLiteral("zoneNumber")] = ruleObj[QLatin1String("zoneNumber")].toInt();
        QString ts = ruleObj[QLatin1String("targetScreen")].toString();
        if (!ts.isEmpty()) {
            rule[QStringLiteral("targetScreen")] = ts;
        }
        result.append(rule);
    }
    return result;
}

void KCMPlasmaZones::setAppRulesForLayout(const QString& layoutId, const QVariantList& rules)
{
    m_pendingAppRules[layoutId] = rules;
    setNeedsSave(true);
}

void KCMPlasmaZones::addAppRuleToLayout(const QString& layoutId, const QString& pattern,
                                         int zoneNumber, const QString& targetScreen)
{
    QString trimmed = pattern.trimmed();
    if (trimmed.isEmpty() || zoneNumber < 1) {
        return;
    }

    QVariantList rules = getAppRulesForLayout(layoutId);

    // Check for duplicate: same pattern AND same targetScreen
    for (const auto& ruleVar : rules) {
        QVariantMap existing = ruleVar.toMap();
        if (existing[QStringLiteral("pattern")].toString().compare(trimmed, Qt::CaseInsensitive) == 0
            && existing[QStringLiteral("targetScreen")].toString() == targetScreen) {
            return;
        }
    }

    QVariantMap newRule;
    newRule[QStringLiteral("pattern")] = trimmed;
    newRule[QStringLiteral("zoneNumber")] = zoneNumber;
    if (!targetScreen.isEmpty()) {
        newRule[QStringLiteral("targetScreen")] = targetScreen;
    }
    rules.append(newRule);
    setAppRulesForLayout(layoutId, rules);
}

void KCMPlasmaZones::removeAppRuleFromLayout(const QString& layoutId, int index)
{
    QVariantList rules = getAppRulesForLayout(layoutId);
    if (index < 0 || index >= rules.size()) {
        return;
    }
    rules.removeAt(index);
    setAppRulesForLayout(layoutId, rules);
}

} // namespace PlasmaZones

#include "kcm_plasmazones.moc"
