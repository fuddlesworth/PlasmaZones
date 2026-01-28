// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "kcm_plasmazones.h"
#include "../src/config/settings.h"
#include "../src/core/constants.h"
#include "../src/core/interfaces.h" // For DragModifier enum
#include "../src/core/logging.h"

#include <KPluginFactory>
#include <KLocalizedString>
#include <KConfig>
#include <KConfigGroup>
#include <KSharedConfig>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingReply>
#include <QDBusPendingCallWatcher>
#include <QStandardPaths>
#include <QTimer>
#include <QFile>
#include <QDir>
#include <QProcess>
#include <QDBusConnectionInterface>
#include <QDBusReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QtGui/QtGui> // For Qt::KeyboardModifier flags
#include <KGlobalAccel>
#include <QtQml/qqmlextensionplugin.h>

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

    // Set up daemon status polling
    m_daemonCheckTimer = new QTimer(this);
    m_daemonCheckTimer->setInterval(KCMConstants::DaemonStatusPollIntervalMs);
    connect(m_daemonCheckTimer, &QTimer::timeout, this, &KCMPlasmaZones::checkDaemonStatus);
    m_daemonCheckTimer->start();

    // Load daemon enabled state from systemd (async)
    m_lastDaemonState = isDaemonRunning();
    m_daemonEnabled = m_lastDaemonState; // Assume enabled if running, will be corrected async
    refreshDaemonEnabledState();

    // Listen for layout changes from the daemon
    // When layouts are edited and saved, the daemon emits layoutListChanged
    // which triggers a refresh of the layout list in the settings panel
    QDBusConnection::sessionBus().connect(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                          QString(DBus::Interface::LayoutManager), QStringLiteral("layoutListChanged"),
                                          this, SLOT(loadLayouts()));

    // Also listen for individual layout changes (when a specific layout is updated)
    QDBusConnection::sessionBus().connect(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                          QString(DBus::Interface::LayoutManager), QStringLiteral("layoutChanged"),
                                          this, SLOT(loadLayouts()));

    // Listen for daemon ready signal (emitted when daemon finishes initialization)
    QDBusConnection::sessionBus().connect(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                          QString(DBus::Interface::LayoutManager), QStringLiteral("daemonReady"), this,
                                          SLOT(loadLayouts()));

    // Listen for active layout ID changes (e.g., when layout changes via hotkey)
    // This updates the selection in the settings panel to match the current layout
    bool activeLayoutConnected = QDBusConnection::sessionBus().connect(
        QString(DBus::ServiceName), QString(DBus::ObjectPath), QString(DBus::Interface::LayoutManager),
        QStringLiteral("activeLayoutIdChanged"), this, SLOT(onActiveLayoutIdChanged(QString)));
    Q_UNUSED(activeLayoutConnected);

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

    // Initial virtual desktop refresh
    refreshVirtualDesktops();
}

KCMPlasmaZones::~KCMPlasmaZones()
{
}

// Activation getters
bool KCMPlasmaZones::shiftDragToActivate() const
{
    return m_settings->shiftDragToActivate();
}
int KCMPlasmaZones::dragActivationModifier() const
{
    // Convert DragModifier enum to Qt::KeyboardModifier bitmask for UI
    return dragModifierToBitmask(static_cast<int>(m_settings->dragActivationModifier()));
}
int KCMPlasmaZones::multiZoneModifier() const
{
    // Convert DragModifier enum to Qt::KeyboardModifier bitmask for UI
    return dragModifierToBitmask(static_cast<int>(m_settings->multiZoneModifier()));
}
bool KCMPlasmaZones::middleClickMultiZone() const
{
    return m_settings->middleClickMultiZone();
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
QColor KCMPlasmaZones::numberColor() const
{
    return m_settings->numberColor();
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
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
    KConfigGroup editorGroup = config->group(QStringLiteral("Editor"));
    return editorGroup.readEntry("EditorDuplicateShortcut", QStringLiteral("Ctrl+D"));
}

QString KCMPlasmaZones::editorSplitHorizontalShortcut() const
{
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
    KConfigGroup editorGroup = config->group(QStringLiteral("Editor"));
    return editorGroup.readEntry("EditorSplitHorizontalShortcut", QStringLiteral("Ctrl+Shift+H"));
}

QString KCMPlasmaZones::editorSplitVerticalShortcut() const
{
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
    KConfigGroup editorGroup = config->group(QStringLiteral("Editor"));
    // Note: Default changed from Ctrl+Shift+V to Ctrl+Alt+V to avoid conflict with Paste with Offset
    return editorGroup.readEntry("EditorSplitVerticalShortcut", QStringLiteral("Ctrl+Alt+V"));
}

QString KCMPlasmaZones::editorFillShortcut() const
{
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
    KConfigGroup editorGroup = config->group(QStringLiteral("Editor"));
    return editorGroup.readEntry("EditorFillShortcut", QStringLiteral("Ctrl+Shift+F"));
}

// Editor snapping settings getters (read from KConfig Editor group)
bool KCMPlasmaZones::editorGridSnappingEnabled() const
{
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
    KConfigGroup editorGroup = config->group(QStringLiteral("Editor"));
    return editorGroup.readEntry("GridSnappingEnabled", true);
}

bool KCMPlasmaZones::editorEdgeSnappingEnabled() const
{
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
    KConfigGroup editorGroup = config->group(QStringLiteral("Editor"));
    return editorGroup.readEntry("EdgeSnappingEnabled", true);
}

qreal KCMPlasmaZones::editorSnapIntervalX() const
{
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
    KConfigGroup editorGroup = config->group(QStringLiteral("Editor"));
    qreal intervalX = editorGroup.readEntry("SnapIntervalX", -1.0);
    if (intervalX < 0.0) {
        // Fall back to single SnapInterval for backward compatibility
        intervalX = editorGroup.readEntry("SnapInterval", 0.1);
    }
    return intervalX;
}

qreal KCMPlasmaZones::editorSnapIntervalY() const
{
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
    KConfigGroup editorGroup = config->group(QStringLiteral("Editor"));
    qreal intervalY = editorGroup.readEntry("SnapIntervalY", -1.0);
    if (intervalY < 0.0) {
        // Fall back to single SnapInterval for backward compatibility
        intervalY = editorGroup.readEntry("SnapInterval", 0.1);
    }
    return intervalY;
}

int KCMPlasmaZones::editorSnapOverrideModifier() const
{
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
    KConfigGroup editorGroup = config->group(QStringLiteral("Editor"));
    return editorGroup.readEntry("SnapOverrideModifier", 0x02000000); // Qt::ShiftModifier
}

// Fill on drop getters (read from KConfig Editor group)
bool KCMPlasmaZones::fillOnDropEnabled() const
{
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
    KConfigGroup editorGroup = config->group(QStringLiteral("Editor"));
    return editorGroup.readEntry("FillOnDropEnabled", true);
}

int KCMPlasmaZones::fillOnDropModifier() const
{
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
    KConfigGroup editorGroup = config->group(QStringLiteral("Editor"));
    return editorGroup.readEntry("FillOnDropModifier", 0x04000000); // Qt::ControlModifier
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
void KCMPlasmaZones::setShiftDragToActivate(bool enable)
{
    if (m_settings->shiftDragToActivate() != enable) {
        m_settings->setShiftDragToActivate(enable);
        Q_EMIT shiftDragToActivateChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setDragActivationModifier(int bitmask)
{
    // Convert Qt::KeyboardModifier bitmask to DragModifier enum for storage
    int enumValue = bitmaskToDragModifier(bitmask);
    if (static_cast<int>(m_settings->dragActivationModifier()) != enumValue) {
        m_settings->setDragActivationModifier(static_cast<DragModifier>(enumValue));
        Q_EMIT dragActivationModifierChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setMultiZoneModifier(int bitmask)
{
    // Convert Qt::KeyboardModifier bitmask to DragModifier enum for storage
    int enumValue = bitmaskToDragModifier(bitmask);
    if (static_cast<int>(m_settings->multiZoneModifier()) != enumValue) {
        m_settings->setMultiZoneModifier(static_cast<DragModifier>(enumValue));
        Q_EMIT multiZoneModifierChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setMiddleClickMultiZone(bool enable)
{
    if (m_settings->middleClickMultiZone() != enable) {
        m_settings->setMiddleClickMultiZone(enable);
        Q_EMIT middleClickMultiZoneChanged();
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

void KCMPlasmaZones::setNumberColor(const QColor& color)
{
    if (m_settings->numberColor() != color) {
        m_settings->setNumberColor(color);
        Q_EMIT numberColorChanged();
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

// Editor shortcuts setters (write directly to KConfig Editor group)
// Note: Save, Delete, Close shortcuts now use Qt StandardKey (system shortcuts)
void KCMPlasmaZones::setEditorDuplicateShortcut(const QString& shortcut)
{
    if (editorDuplicateShortcut() != shortcut) {
        auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
        KConfigGroup editorGroup = config->group(QStringLiteral("Editor"));
        editorGroup.writeEntry("EditorDuplicateShortcut", shortcut);
        config->sync();
        Q_EMIT editorDuplicateShortcutChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setEditorSplitHorizontalShortcut(const QString& shortcut)
{
    if (editorSplitHorizontalShortcut() != shortcut) {
        auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
        KConfigGroup editorGroup = config->group(QStringLiteral("Editor"));
        editorGroup.writeEntry("EditorSplitHorizontalShortcut", shortcut);
        config->sync();
        Q_EMIT editorSplitHorizontalShortcutChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setEditorSplitVerticalShortcut(const QString& shortcut)
{
    if (editorSplitVerticalShortcut() != shortcut) {
        auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
        KConfigGroup editorGroup = config->group(QStringLiteral("Editor"));
        editorGroup.writeEntry("EditorSplitVerticalShortcut", shortcut);
        config->sync();
        Q_EMIT editorSplitVerticalShortcutChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setEditorFillShortcut(const QString& shortcut)
{
    if (editorFillShortcut() != shortcut) {
        auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
        KConfigGroup editorGroup = config->group(QStringLiteral("Editor"));
        editorGroup.writeEntry("EditorFillShortcut", shortcut);
        config->sync();
        Q_EMIT editorFillShortcutChanged();
        setNeedsSave(true);
    }
}

// Editor snapping settings setters (write to KConfig Editor group)
void KCMPlasmaZones::setEditorGridSnappingEnabled(bool enabled)
{
    if (editorGridSnappingEnabled() != enabled) {
        auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
        KConfigGroup editorGroup = config->group(QStringLiteral("Editor"));
        editorGroup.writeEntry("GridSnappingEnabled", enabled);
        config->sync();
        Q_EMIT editorGridSnappingEnabledChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setEditorEdgeSnappingEnabled(bool enabled)
{
    if (editorEdgeSnappingEnabled() != enabled) {
        auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
        KConfigGroup editorGroup = config->group(QStringLiteral("Editor"));
        editorGroup.writeEntry("EdgeSnappingEnabled", enabled);
        config->sync();
        Q_EMIT editorEdgeSnappingEnabledChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setEditorSnapIntervalX(qreal interval)
{
    interval = qBound(0.01, interval, 1.0);
    if (!qFuzzyCompare(editorSnapIntervalX(), interval)) {
        auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
        KConfigGroup editorGroup = config->group(QStringLiteral("Editor"));
        editorGroup.writeEntry("SnapIntervalX", interval);
        config->sync();
        Q_EMIT editorSnapIntervalXChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setEditorSnapIntervalY(qreal interval)
{
    interval = qBound(0.01, interval, 1.0);
    if (!qFuzzyCompare(editorSnapIntervalY(), interval)) {
        auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
        KConfigGroup editorGroup = config->group(QStringLiteral("Editor"));
        editorGroup.writeEntry("SnapIntervalY", interval);
        config->sync();
        Q_EMIT editorSnapIntervalYChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setEditorSnapOverrideModifier(int modifier)
{
    if (editorSnapOverrideModifier() != modifier) {
        auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
        KConfigGroup editorGroup = config->group(QStringLiteral("Editor"));
        editorGroup.writeEntry("SnapOverrideModifier", modifier);
        config->sync();
        Q_EMIT editorSnapOverrideModifierChanged();
        setNeedsSave(true);
    }
}

// Fill on drop setters (write to KConfig Editor group)
void KCMPlasmaZones::setFillOnDropEnabled(bool enabled)
{
    if (fillOnDropEnabled() != enabled) {
        auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
        KConfigGroup editorGroup = config->group(QStringLiteral("Editor"));
        editorGroup.writeEntry("FillOnDropEnabled", enabled);
        config->sync();
        Q_EMIT fillOnDropEnabledChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::setFillOnDropModifier(int modifier)
{
    if (fillOnDropModifier() != modifier) {
        auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
        KConfigGroup editorGroup = config->group(QStringLiteral("Editor"));
        editorGroup.writeEntry("FillOnDropModifier", modifier);
        config->sync();
        Q_EMIT fillOnDropModifierChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::save()
{
    m_settings->save();

    // Screen assignments and quick layout slots are owned by the daemon.
    // We only send changes via D-Bus; the daemon persists to assignments.json.

    int errorCount = 0;
    const QString layoutInterface = QString(DBus::Interface::LayoutManager);

    // Apply screen assignments to daemon via D-Bus
    // First, clear assignments for screens that don't have a layout assigned
    for (const QVariant& screenVar : std::as_const(m_screens)) {
        QVariantMap screen = screenVar.toMap();
        QString screenName = screen.value(QStringLiteral("name")).toString();
        if (!screenName.isEmpty() && !m_screenAssignments.contains(screenName)) {
            QDBusMessage reply = callDaemon(layoutInterface, QStringLiteral("clearAssignment"), {screenName});
            if (reply.type() == QDBusMessage::ErrorMessage) {
                ++errorCount;
            }
        }
    }

    // Then set the assignments
    for (auto it = m_screenAssignments.begin(); it != m_screenAssignments.end(); ++it) {
        QString layoutId = it.value().toString();
        QDBusMessage reply = callDaemon(layoutInterface, QStringLiteral("assignLayoutToScreen"), {it.key(), layoutId});
        if (reply.type() == QDBusMessage::ErrorMessage) {
            ++errorCount;
        }
    }

    // Apply quick layout slots to daemon via D-Bus
    // Send all 9 slots - empty string clears the slot on daemon
    for (int slot = 1; slot <= 9; ++slot) {
        QString layoutId = m_quickLayoutSlots.value(slot, QString());
        QDBusMessage reply = callDaemon(layoutInterface, QStringLiteral("setQuickLayoutSlot"), {slot, layoutId});
        if (reply.type() == QDBusMessage::ErrorMessage) {
            ++errorCount;
        }
    }

    // Apply pending per-desktop assignments to daemon via D-Bus
    // First, process cleared assignments
    for (const QString& key : std::as_const(m_clearedDesktopAssignments)) {
        QStringList parts = key.split(QLatin1Char(':'));
        if (parts.size() == 2) {
            QString screenName = parts[0];
            int virtualDesktop = parts[1].toInt();
            QDBusMessage reply = callDaemon(layoutInterface, QStringLiteral("clearAssignmentForScreenDesktop"),
                                            {screenName, virtualDesktop});
            if (reply.type() == QDBusMessage::ErrorMessage) {
                ++errorCount;
            }
        }
    }
    m_clearedDesktopAssignments.clear();

    // Then, apply pending assignments
    for (auto it = m_pendingDesktopAssignments.begin(); it != m_pendingDesktopAssignments.end(); ++it) {
        QStringList parts = it.key().split(QLatin1Char(':'));
        if (parts.size() == 2) {
            QString screenName = parts[0];
            int virtualDesktop = parts[1].toInt();
            QString layoutId = it.value();
            QDBusMessage reply = callDaemon(layoutInterface, QStringLiteral("assignLayoutToScreenDesktop"),
                                            {screenName, virtualDesktop, layoutId});
            if (reply.type() == QDBusMessage::ErrorMessage) {
                ++errorCount;
            }
        }
    }
    m_pendingDesktopAssignments.clear();

    if (errorCount > 0) {
        qCWarning(lcKcm) << "Save:" << errorCount
                         << "D-Bus call(s) failed - some settings may not have been saved to daemon";
    }

    notifyDaemon();
    setNeedsSave(false);
}

void KCMPlasmaZones::load()
{
    m_settings->load();
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

    // Clear pending per-desktop assignments (discard unsaved changes)
    m_pendingDesktopAssignments.clear();
    m_clearedDesktopAssignments.clear();

    Q_EMIT screenAssignmentsChanged();
    Q_EMIT quickLayoutSlotsRefreshed();
    setNeedsSave(false);
}

void KCMPlasmaZones::defaults()
{
    m_settings->reset();

    // Find "Columns (2)" layout and set it as the default
    for (const QVariant& layoutVar : m_layouts) {
        const QVariantMap layout = layoutVar.toMap();
        if (layout[QStringLiteral("name")].toString() == QStringLiteral("Columns (2)")) {
            m_settings->setDefaultLayoutId(layout[QStringLiteral("id")].toString());
            break;
        }
    }

    // Clear screen assignments
    m_screenAssignments.clear();

    // Clear quick layout slots
    m_quickLayoutSlots.clear();

    // Clear pending per-desktop assignments
    m_pendingDesktopAssignments.clear();
    m_clearedDesktopAssignments.clear();

    // Emit all property change signals so UI updates
    Q_EMIT screenAssignmentsChanged();
    Q_EMIT shiftDragToActivateChanged();
    Q_EMIT dragActivationModifierChanged();
    Q_EMIT multiZoneModifierChanged();
    Q_EMIT middleClickMultiZoneChanged();
    Q_EMIT showZonesOnAllMonitorsChanged();
    Q_EMIT disabledMonitorsChanged();
    Q_EMIT showZoneNumbersChanged();
    Q_EMIT flashZonesOnSwitchChanged();
    Q_EMIT showOsdOnLayoutSwitchChanged();
    Q_EMIT useSystemColorsChanged();
    Q_EMIT highlightColorChanged();
    Q_EMIT inactiveColorChanged();
    Q_EMIT borderColorChanged();
    Q_EMIT numberColorChanged();
    Q_EMIT activeOpacityChanged();
    Q_EMIT inactiveOpacityChanged();
    Q_EMIT borderWidthChanged();
    Q_EMIT borderRadiusChanged();
    Q_EMIT enableBlurChanged();
    Q_EMIT enableShaderEffectsChanged();
    Q_EMIT shaderFrameRateChanged();
    Q_EMIT zonePaddingChanged();
    Q_EMIT outerGapChanged();
    Q_EMIT adjacentThresholdChanged();
    Q_EMIT keepWindowsInZonesOnResolutionChangeChanged();
    Q_EMIT moveNewWindowsToLastZoneChanged();
    Q_EMIT restoreOriginalSizeOnUnsnapChanged();
    Q_EMIT stickyWindowHandlingChanged();
    Q_EMIT defaultLayoutIdChanged();
    Q_EMIT excludedApplicationsChanged();
    Q_EMIT excludedWindowClassesChanged();
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

    // Reload layouts after a short delay to allow the layout to be fully created
    QTimer::singleShot(100, this, &KCMPlasmaZones::loadLayouts);
}

void KCMPlasmaZones::deleteLayout(const QString& layoutId)
{
    QDBusMessage msg =
        QDBusMessage::createMethodCall(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                       QString(DBus::Interface::LayoutManager), QStringLiteral("deleteLayout"));
    msg << layoutId;
    QDBusConnection::sessionBus().asyncCall(msg);
    QTimer::singleShot(100, this, &KCMPlasmaZones::loadLayouts);
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

    // Reload layouts after a short delay to allow the layout to be fully created
    QTimer::singleShot(100, this, &KCMPlasmaZones::loadLayouts);
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

    // Reload layouts after a short delay to allow the layout to be fully created
    QTimer::singleShot(100, this, &KCMPlasmaZones::loadLayouts);
}

void KCMPlasmaZones::exportLayout(const QString& layoutId, const QString& filePath)
{
    QDBusMessage msg =
        QDBusMessage::createMethodCall(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                       QString(DBus::Interface::LayoutManager), QStringLiteral("exportLayout"));
    msg << layoutId << filePath;
    QDBusConnection::sessionBus().asyncCall(msg);
}

void KCMPlasmaZones::editLayout(const QString& layoutId)
{
    QDBusMessage msg =
        QDBusMessage::createMethodCall(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                       QString(DBus::Interface::LayoutManager), QStringLiteral("openEditorForLayout"));
    msg << layoutId;
    QDBusConnection::sessionBus().asyncCall(msg);
}

void KCMPlasmaZones::openEditor()
{
    QDBusMessage msg =
        QDBusMessage::createMethodCall(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                       QString(DBus::Interface::LayoutManager), QStringLiteral("openEditor"));
    QDBusConnection::sessionBus().asyncCall(msg);
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

void KCMPlasmaZones::loadColorsFromPywal()
{
    QString pywalPath = QDir::homePath() + QStringLiteral("/.cache/wal/colors.json");
    if (QFile::exists(pywalPath)) {
        m_settings->loadColorsFromFile(pywalPath);
        Q_EMIT highlightColorChanged();
        Q_EMIT inactiveColorChanged();
        Q_EMIT borderColorChanged();
        Q_EMIT numberColorChanged();
        setNeedsSave(true);
    }
}

void KCMPlasmaZones::loadColorsFromFile(const QString& filePath)
{
    m_settings->loadColorsFromFile(filePath);
    Q_EMIT highlightColorChanged();
    Q_EMIT inactiveColorChanged();
    Q_EMIT borderColorChanged();
    Q_EMIT numberColorChanged();
    setNeedsSave(true);
}

void KCMPlasmaZones::resetEditorShortcuts()
{
    // Force set all app-specific editor shortcuts to defaults (always emit signals)
    // Note: Save, Delete, Close shortcuts use Qt StandardKey (system shortcuts) and are not configurable
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
    KConfigGroup editorGroup = config->group(QStringLiteral("Editor"));

    editorGroup.writeEntry("EditorDuplicateShortcut", QStringLiteral("Ctrl+D"));
    editorGroup.writeEntry("EditorSplitHorizontalShortcut", QStringLiteral("Ctrl+Shift+H"));
    editorGroup.writeEntry(
        "EditorSplitVerticalShortcut",
        QStringLiteral("Ctrl+Alt+V")); // Note: Changed from Ctrl+Shift+V to avoid conflict with Paste with Offset
    editorGroup.writeEntry("EditorFillShortcut", QStringLiteral("Ctrl+Shift+F"));

    config->sync();

    // Always emit signals to update UI
    Q_EMIT editorDuplicateShortcutChanged();
    Q_EMIT editorSplitHorizontalShortcutChanged();
    Q_EMIT editorSplitVerticalShortcutChanged();
    Q_EMIT editorFillShortcutChanged();

    setNeedsSave(true);
}

// Helper functions to convert DragModifier enum to/from Qt::KeyboardModifier bitmask
int KCMPlasmaZones::dragModifierToBitmask(int enumValue)
{
    // Qt modifier flags
    constexpr int ShiftModifier = 0x02000000;
    constexpr int ControlModifier = 0x04000000;
    constexpr int AltModifier = 0x08000000;
    constexpr int MetaModifier = 0x10000000;

    switch (enumValue) {
    case 0:
        return 0; // None
    case 1:
        return ShiftModifier; // Shift
    case 2:
        return ControlModifier; // Ctrl
    case 3:
        return AltModifier; // Alt
    case 4:
        return MetaModifier; // Meta
    case 5:
        return ControlModifier | AltModifier; // Ctrl+Alt
    case 6:
        return ControlModifier | ShiftModifier; // Ctrl+Shift
    case 7:
        return AltModifier | ShiftModifier; // Alt+Shift
    case 8:
        return 0; // AlwaysActive - no modifier keys in checkbox UI
    case 9:
        return AltModifier | MetaModifier; // Alt+Meta
    case 10:
        return ControlModifier | AltModifier | MetaModifier; // Ctrl+Alt+Meta
    default:
        return 0;
    }
}

int KCMPlasmaZones::bitmaskToDragModifier(int bitmask)
{
    // Qt modifier flags
    constexpr int ShiftModifier = 0x02000000;
    constexpr int ControlModifier = 0x04000000;
    constexpr int AltModifier = 0x08000000;
    constexpr int MetaModifier = 0x10000000;

    if (bitmask == 0)
        return 0; // None

    bool hasShift = (bitmask & ShiftModifier) != 0;
    bool hasCtrl = (bitmask & ControlModifier) != 0;
    bool hasAlt = (bitmask & AltModifier) != 0;
    bool hasMeta = (bitmask & MetaModifier) != 0;

    // Single modifiers
    if (hasShift && !hasCtrl && !hasAlt && !hasMeta)
        return 1; // Shift
    if (hasCtrl && !hasShift && !hasAlt && !hasMeta)
        return 2; // Ctrl
    if (hasAlt && !hasCtrl && !hasShift && !hasMeta)
        return 3; // Alt
    if (hasMeta && !hasCtrl && !hasAlt && !hasShift)
        return 4; // Meta

    // Two- and three-modifier combinations
    if (hasAlt && hasMeta && !hasCtrl && !hasShift)
        return 9; // Alt+Meta
    if (hasCtrl && hasAlt && hasMeta && !hasShift)
        return 10; // Ctrl+Alt+Meta
    if (hasCtrl && hasAlt && !hasShift && !hasMeta)
        return 5; // Ctrl+Alt
    if (hasCtrl && hasShift && !hasAlt && !hasMeta)
        return 6; // Ctrl+Shift
    if (hasAlt && hasShift && !hasCtrl && !hasMeta)
        return 7; // Alt+Shift

    // For other combinations not in enum, map to closest match or default
    // This allows UI flexibility while maintaining enum compatibility
    if (hasCtrl && hasAlt && hasMeta)
        return 10; // Ctrl+Alt+Meta (e.g. all four modifiers)
    if (hasCtrl && hasAlt)
        return 5; // Ctrl+Alt (closest)
    if (hasCtrl && hasShift)
        return 6; // Ctrl+Shift (closest)
    if (hasAlt && hasShift)
        return 7; // Alt+Shift (closest)
    if (hasCtrl)
        return 2; // Default to Ctrl
    if (hasAlt)
        return 3; // Default to Alt
    if (hasShift)
        return 1; // Default to Shift
    if (hasMeta)
        return 4; // Default to Meta

    return 0; // None
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

    m_layouts = newLayouts;
    Q_EMIT layoutsChanged();

    // Fetch the active layout from the daemon
    if (!newLayouts.isEmpty()) {
        QDBusMessage activeReply =
            callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("getActiveLayout"));
        if (activeReply.type() == QDBusMessage::ReplyMessage && !activeReply.arguments().isEmpty()) {
            QString activeLayoutJson = activeReply.arguments().first().toString();
            if (!activeLayoutJson.isEmpty()) {
                QJsonDocument doc = QJsonDocument::fromJson(activeLayoutJson.toUtf8());
                if (doc.isObject()) {
                    QString activeId = doc.object().value(QStringLiteral("id")).toString();
                    if (!activeId.isEmpty()) {
                        m_layoutToSelect = activeId;
                    }
                }
            }
        }
    }

    // Emit layoutToSelectChanged after layoutsChanged so the model is updated first
    if (!m_layoutToSelect.isEmpty()) {
        Q_EMIT layoutToSelectChanged();
    }
}

void KCMPlasmaZones::onActiveLayoutIdChanged(const QString& layoutId)
{
    // When active layout changes externally (e.g., via quick layout hotkey),
    // update the selection in the settings panel UI
    if (!layoutId.isEmpty()) {
        m_layoutToSelect = layoutId;
        Q_EMIT layoutToSelectChanged();
    }
}

void KCMPlasmaZones::onScreenLayoutChanged(const QString& screenName, const QString& layoutId)
{
    // When screen layout assignment changes externally, update our local state
    if (screenName.isEmpty()) {
        return;
    }

    if (layoutId.isEmpty()) {
        m_screenAssignments.remove(screenName);
    } else {
        m_screenAssignments[screenName] = layoutId;
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

        // Notify QML that quick layout slots have been updated
        Q_EMIT quickLayoutSlotsRefreshed();
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
        Q_EMIT shiftDragToActivateChanged();
        Q_EMIT dragActivationModifierChanged();
        Q_EMIT multiZoneModifierChanged();
        Q_EMIT middleClickMultiZoneChanged();
        Q_EMIT showZonesOnAllMonitorsChanged();
        Q_EMIT disabledMonitorsChanged();
        Q_EMIT showZoneNumbersChanged();
        Q_EMIT flashZonesOnSwitchChanged();
        Q_EMIT useSystemColorsChanged();
        Q_EMIT highlightColorChanged();
        Q_EMIT inactiveColorChanged();
        Q_EMIT borderColorChanged();
        Q_EMIT numberColorChanged();
        Q_EMIT activeOpacityChanged();
        Q_EMIT inactiveOpacityChanged();
        Q_EMIT borderWidthChanged();
        Q_EMIT borderRadiusChanged();
        Q_EMIT enableBlurChanged();
        Q_EMIT zonePaddingChanged();
        Q_EMIT outerGapChanged();
        Q_EMIT adjacentThresholdChanged();
        Q_EMIT keepWindowsInZonesOnResolutionChangeChanged();
        Q_EMIT moveNewWindowsToLastZoneChanged();
        Q_EMIT restoreOriginalSizeOnUnsnapChanged();
        Q_EMIT stickyWindowHandlingChanged();
        Q_EMIT defaultLayoutIdChanged();
        Q_EMIT excludedApplicationsChanged();
        Q_EMIT excludedWindowClassesChanged();
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
                    QVariantMap screenInfo = doc.object().toVariantMap();
                    screenInfo[QStringLiteral("name")] = screenName;
                    newScreens.append(screenInfo);
                } else {
                    // If JSON parsing fails, create minimal screen info
                    QVariantMap screenInfo;
                    screenInfo[QStringLiteral("name")] = screenName;
                    newScreens.append(screenInfo);
                }
            } else {
                // If D-Bus call fails, create minimal screen info
                QVariantMap screenInfo;
                screenInfo[QStringLiteral("name")] = screenName;
                newScreens.append(screenInfo);
            }
        }
    }

    // Fallback: if no screens from daemon, get from Qt
    if (newScreens.isEmpty()) {
        for (QScreen* screen : QGuiApplication::screens()) {
            QVariantMap screenInfo;
            screenInfo[QStringLiteral("name")] = screen->name();
            screenInfo[QStringLiteral("geometry")] = screen->geometry();
            screenInfo[QStringLiteral("primary")] = (screen == QGuiApplication::primaryScreen());
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
    QStringList list = m_settings->disabledMonitors();
    if (disabled) {
        if (!list.contains(screenName)) {
            list.append(screenName);
            m_settings->setDisabledMonitors(list);
            Q_EMIT disabledMonitorsChanged();
            setNeedsSave(true);
        }
    } else {
        if (list.removeAll(screenName) > 0) {
            m_settings->setDisabledMonitors(list);
            Q_EMIT disabledMonitorsChanged();
            setNeedsSave(true);
        }
    }
}

void KCMPlasmaZones::assignLayoutToScreenDesktop(const QString& screenName, int virtualDesktop, const QString& layoutId)
{
    if (screenName.isEmpty()) {
        qCWarning(lcKcm) << "Cannot assign layout - empty screen name";
        return;
    }

    // Cache the assignment locally - will be sent to daemon on Apply
    QString key = QStringLiteral("%1:%2").arg(screenName).arg(virtualDesktop);

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
    QString key = QStringLiteral("%1:%2").arg(screenName).arg(virtualDesktop);
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
    QString key = QStringLiteral("%1:%2").arg(screenName).arg(virtualDesktop);
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
    QString key = QStringLiteral("%1:%2").arg(screenName).arg(virtualDesktop);
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

QString KCMPlasmaZones::getAllScreenAssignmentsJson() const
{
    // Query daemon for all screen assignments as JSON
    QDBusMessage reply = callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("getAllScreenAssignments"));

    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        return reply.arguments().first().toString();
    }

    return QStringLiteral("{}");
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

} // namespace PlasmaZones

#include "kcm_plasmazones.moc"
