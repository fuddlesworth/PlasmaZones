// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settings.h"
#include "configdefaults.h"
#include "../core/constants.h"
#include "../core/logging.h"
#include <KConfig>
#include <KConfigGroup>
#include <KSharedConfig>
#include <KColorScheme>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

namespace PlasmaZones {

namespace {
/**
 * @brief Normalize a UUID string to use the default format (with braces)
 *
 * Handles migration from configs saved with WithoutBraces format.
 * Returns empty string if input is empty or not a valid UUID.
 */
QString normalizeUuidString(const QString& uuidStr)
{
    if (uuidStr.isEmpty()) {
        return QString();
    }

    // Parse as UUID (handles both with and without braces)
    QUuid uuid = QUuid::fromString(uuidStr);
    if (uuid.isNull()) {
        qCWarning(lcConfig) << "Invalid UUID string in config, ignoring:" << uuidStr;
        return QString();
    }

    // Return in default format (with braces) for consistent comparison
    return uuid.toString();
}
} // anonymous namespace

Settings::Settings(QObject* parent)
    : ISettings(parent)
{
    load();
}

void Settings::setShiftDragToActivate(bool enable)
{
    if (m_shiftDragToActivate != enable) {
        m_shiftDragToActivate = enable;
        // Migrate to new setting
        if (enable) {
            setDragActivationModifier(DragModifier::Shift);
        }
        Q_EMIT shiftDragToActivateChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setDragActivationModifier(DragModifier modifier)
{
    if (m_dragActivationModifier != modifier) {
        m_dragActivationModifier = modifier;
        Q_EMIT dragActivationModifierChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setDragActivationModifierInt(int modifier)
{
    if (modifier >= 0 && modifier <= static_cast<int>(DragModifier::CtrlAltMeta)) {
        setDragActivationModifier(static_cast<DragModifier>(modifier));
    }
}

void Settings::setSkipSnapModifier(DragModifier modifier)
{
    if (m_skipSnapModifier != modifier) {
        m_skipSnapModifier = modifier;
        Q_EMIT skipSnapModifierChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setSkipSnapModifierInt(int modifier)
{
    if (modifier >= 0 && modifier <= static_cast<int>(DragModifier::AlwaysActive)) {
        setSkipSnapModifier(static_cast<DragModifier>(modifier));
    }
}

void Settings::setMultiZoneModifier(DragModifier modifier)
{
    if (m_multiZoneModifier != modifier) {
        m_multiZoneModifier = modifier;
        Q_EMIT multiZoneModifierChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setMultiZoneModifierInt(int modifier)
{
    if (modifier >= 0 && modifier <= static_cast<int>(DragModifier::CtrlAltMeta)) {
        setMultiZoneModifier(static_cast<DragModifier>(modifier));
    }
}

void Settings::setMiddleClickMultiZone(bool enable)
{
    if (m_middleClickMultiZone != enable) {
        m_middleClickMultiZone = enable;
        Q_EMIT middleClickMultiZoneChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setShowZonesOnAllMonitors(bool show)
{
    if (m_showZonesOnAllMonitors != show) {
        m_showZonesOnAllMonitors = show;
        Q_EMIT showZonesOnAllMonitorsChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setDisabledMonitors(const QStringList& screenNames)
{
    if (m_disabledMonitors != screenNames) {
        m_disabledMonitors = screenNames;
        Q_EMIT disabledMonitorsChanged();
        Q_EMIT settingsChanged();
    }
}

bool Settings::isMonitorDisabled(const QString& screenName) const
{
    return m_disabledMonitors.contains(screenName);
}

void Settings::setShowZoneNumbers(bool show)
{
    if (m_showZoneNumbers != show) {
        m_showZoneNumbers = show;
        Q_EMIT showZoneNumbersChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setFlashZonesOnSwitch(bool flash)
{
    if (m_flashZonesOnSwitch != flash) {
        m_flashZonesOnSwitch = flash;
        Q_EMIT flashZonesOnSwitchChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setShowOsdOnLayoutSwitch(bool show)
{
    if (m_showOsdOnLayoutSwitch != show) {
        m_showOsdOnLayoutSwitch = show;
        Q_EMIT showOsdOnLayoutSwitchChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setShowNavigationOsd(bool show)
{
    if (m_showNavigationOsd != show) {
        m_showNavigationOsd = show;
        Q_EMIT showNavigationOsdChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setOsdStyle(OsdStyle style)
{
    if (m_osdStyle != style) {
        m_osdStyle = style;
        Q_EMIT osdStyleChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setOsdStyleInt(int style)
{
    setOsdStyle(static_cast<OsdStyle>(style));
}

void Settings::setUseSystemColors(bool use)
{
    if (m_useSystemColors != use) {
        m_useSystemColors = use;
        if (use) {
            applySystemColorScheme();
        }
        Q_EMIT useSystemColorsChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setHighlightColor(const QColor& color)
{
    if (m_highlightColor != color) {
        m_highlightColor = color;
        Q_EMIT highlightColorChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setInactiveColor(const QColor& color)
{
    if (m_inactiveColor != color) {
        m_inactiveColor = color;
        Q_EMIT inactiveColorChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setBorderColor(const QColor& color)
{
    if (m_borderColor != color) {
        m_borderColor = color;
        Q_EMIT borderColorChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setNumberColor(const QColor& color)
{
    if (m_numberColor != color) {
        m_numberColor = color;
        Q_EMIT numberColorChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setActiveOpacity(qreal opacity)
{
    opacity = qBound(0.0, opacity, 1.0);
    if (!qFuzzyCompare(m_activeOpacity, opacity)) {
        m_activeOpacity = opacity;
        Q_EMIT activeOpacityChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setInactiveOpacity(qreal opacity)
{
    opacity = qBound(0.0, opacity, 1.0);
    if (!qFuzzyCompare(m_inactiveOpacity, opacity)) {
        m_inactiveOpacity = opacity;
        Q_EMIT inactiveOpacityChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setBorderWidth(int width)
{
    width = qMax(0, width);
    if (m_borderWidth != width) {
        m_borderWidth = width;
        Q_EMIT borderWidthChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setBorderRadius(int radius)
{
    radius = qMax(0, radius);
    if (m_borderRadius != radius) {
        m_borderRadius = radius;
        Q_EMIT borderRadiusChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setEnableBlur(bool enable)
{
    if (m_enableBlur != enable) {
        m_enableBlur = enable;
        Q_EMIT enableBlurChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setZonePadding(int padding)
{
    padding = qMax(0, padding);
    if (m_zonePadding != padding) {
        m_zonePadding = padding;
        Q_EMIT zonePaddingChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setOuterGap(int gap)
{
    gap = qMax(0, gap);
    if (m_outerGap != gap) {
        m_outerGap = gap;
        Q_EMIT outerGapChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setAdjacentThreshold(int threshold)
{
    threshold = qMax(0, threshold);
    if (m_adjacentThreshold != threshold) {
        m_adjacentThreshold = threshold;
        Q_EMIT adjacentThresholdChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setPollIntervalMs(int interval)
{
    interval = qBound(10, interval, 1000); // Clamp to 10-1000ms
    if (m_pollIntervalMs != interval) {
        m_pollIntervalMs = interval;
        Q_EMIT pollIntervalMsChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setMinimumZoneSizePx(int size)
{
    size = qBound(50, size, 500); // Clamp to 50-500px
    if (m_minimumZoneSizePx != size) {
        m_minimumZoneSizePx = size;
        Q_EMIT minimumZoneSizePxChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setMinimumZoneDisplaySizePx(int size)
{
    size = qBound(1, size, 50); // Clamp to 1-50px
    if (m_minimumZoneDisplaySizePx != size) {
        m_minimumZoneDisplaySizePx = size;
        Q_EMIT minimumZoneDisplaySizePxChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setKeepWindowsInZonesOnResolutionChange(bool keep)
{
    if (m_keepWindowsInZonesOnResolutionChange != keep) {
        m_keepWindowsInZonesOnResolutionChange = keep;
        Q_EMIT keepWindowsInZonesOnResolutionChangeChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setMoveNewWindowsToLastZone(bool move)
{
    if (m_moveNewWindowsToLastZone != move) {
        m_moveNewWindowsToLastZone = move;
        Q_EMIT moveNewWindowsToLastZoneChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setRestoreOriginalSizeOnUnsnap(bool restore)
{
    if (m_restoreOriginalSizeOnUnsnap != restore) {
        m_restoreOriginalSizeOnUnsnap = restore;
        Q_EMIT restoreOriginalSizeOnUnsnapChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setStickyWindowHandling(StickyWindowHandling handling)
{
    if (m_stickyWindowHandling != handling) {
        m_stickyWindowHandling = handling;
        Q_EMIT stickyWindowHandlingChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setStickyWindowHandlingInt(int handling)
{
    if (handling >= static_cast<int>(StickyWindowHandling::TreatAsNormal)
        && handling <= static_cast<int>(StickyWindowHandling::IgnoreAll)) {
        setStickyWindowHandling(static_cast<StickyWindowHandling>(handling));
    }
}

void Settings::setRestoreWindowsToZonesOnLogin(bool restore)
{
    if (m_restoreWindowsToZonesOnLogin != restore) {
        m_restoreWindowsToZonesOnLogin = restore;
        Q_EMIT restoreWindowsToZonesOnLoginChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setDefaultLayoutId(const QString& layoutId)
{
    // Normalize to default format (with braces) for consistent comparison
    QString normalizedId = normalizeUuidString(layoutId);
    if (m_defaultLayoutId != normalizedId) {
        m_defaultLayoutId = normalizedId;
        Q_EMIT defaultLayoutIdChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setExcludedApplications(const QStringList& apps)
{
    if (m_excludedApplications != apps) {
        m_excludedApplications = apps;
        Q_EMIT excludedApplicationsChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setExcludedWindowClasses(const QStringList& classes)
{
    if (m_excludedWindowClasses != classes) {
        m_excludedWindowClasses = classes;
        Q_EMIT excludedWindowClassesChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setExcludeTransientWindows(bool exclude)
{
    if (m_excludeTransientWindows != exclude) {
        m_excludeTransientWindows = exclude;
        Q_EMIT excludeTransientWindowsChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setMinimumWindowWidth(int width)
{
    width = qBound(0, width, 1000); // Clamp to 0-1000 pixels (0 disables)
    if (m_minimumWindowWidth != width) {
        m_minimumWindowWidth = width;
        Q_EMIT minimumWindowWidthChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setMinimumWindowHeight(int height)
{
    height = qBound(0, height, 1000); // Clamp to 0-1000 pixels (0 disables)
    if (m_minimumWindowHeight != height) {
        m_minimumWindowHeight = height;
        Q_EMIT minimumWindowHeightChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setZoneSelectorEnabled(bool enabled)
{
    if (m_zoneSelectorEnabled != enabled) {
        m_zoneSelectorEnabled = enabled;
        Q_EMIT zoneSelectorEnabledChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setZoneSelectorTriggerDistance(int distance)
{
    distance = qBound(10, distance, 200); // Clamp to 10-200 pixels
    if (m_zoneSelectorTriggerDistance != distance) {
        m_zoneSelectorTriggerDistance = distance;
        Q_EMIT zoneSelectorTriggerDistanceChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setZoneSelectorPosition(ZoneSelectorPosition position)
{
    if (m_zoneSelectorPosition != position) {
        m_zoneSelectorPosition = position;
        Q_EMIT zoneSelectorPositionChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setZoneSelectorPositionInt(int position)
{
    // Valid positions are 0-8 except 4 (center)
    if (position >= 0 && position <= 8 && position != 4) {
        setZoneSelectorPosition(static_cast<ZoneSelectorPosition>(position));
    }
}

void Settings::setZoneSelectorLayoutMode(ZoneSelectorLayoutMode mode)
{
    if (m_zoneSelectorLayoutMode != mode) {
        m_zoneSelectorLayoutMode = mode;
        Q_EMIT zoneSelectorLayoutModeChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setZoneSelectorLayoutModeInt(int mode)
{
    if (mode >= 0 && mode <= static_cast<int>(ZoneSelectorLayoutMode::Vertical)) {
        setZoneSelectorLayoutMode(static_cast<ZoneSelectorLayoutMode>(mode));
    }
}

void Settings::setZoneSelectorPreviewWidth(int width)
{
    width = qBound(80, width, 400);
    if (m_zoneSelectorPreviewWidth != width) {
        m_zoneSelectorPreviewWidth = width;
        Q_EMIT zoneSelectorPreviewWidthChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setZoneSelectorPreviewHeight(int height)
{
    height = qBound(60, height, 300);
    if (m_zoneSelectorPreviewHeight != height) {
        m_zoneSelectorPreviewHeight = height;
        Q_EMIT zoneSelectorPreviewHeightChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setZoneSelectorPreviewLockAspect(bool locked)
{
    if (m_zoneSelectorPreviewLockAspect != locked) {
        m_zoneSelectorPreviewLockAspect = locked;
        Q_EMIT zoneSelectorPreviewLockAspectChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setZoneSelectorGridColumns(int columns)
{
    columns = qBound(1, columns, 10);
    if (m_zoneSelectorGridColumns != columns) {
        m_zoneSelectorGridColumns = columns;
        Q_EMIT zoneSelectorGridColumnsChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setZoneSelectorSizeMode(ZoneSelectorSizeMode mode)
{
    if (m_zoneSelectorSizeMode != mode) {
        m_zoneSelectorSizeMode = mode;
        Q_EMIT zoneSelectorSizeModeChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setZoneSelectorSizeModeInt(int mode)
{
    if (mode >= 0 && mode <= static_cast<int>(ZoneSelectorSizeMode::Manual)) {
        setZoneSelectorSizeMode(static_cast<ZoneSelectorSizeMode>(mode));
    }
}

void Settings::setZoneSelectorMaxRows(int rows)
{
    rows = qBound(1, rows, 10);
    if (m_zoneSelectorMaxRows != rows) {
        m_zoneSelectorMaxRows = rows;
        Q_EMIT zoneSelectorMaxRowsChanged();
        Q_EMIT settingsChanged();
    }
}

// Shader Effects implementations
void Settings::setEnableShaderEffects(bool enable)
{
    if (m_enableShaderEffects != enable) {
        m_enableShaderEffects = enable;
        Q_EMIT enableShaderEffectsChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setShaderFrameRate(int fps)
{
    fps = qBound(30, fps, 144);
    if (m_shaderFrameRate != fps) {
        m_shaderFrameRate = fps;
        Q_EMIT shaderFrameRateChanged();
        Q_EMIT settingsChanged();
    }
}

// Global Shortcuts implementations
void Settings::setOpenEditorShortcut(const QString& shortcut)
{
    if (m_openEditorShortcut != shortcut) {
        m_openEditorShortcut = shortcut;
        Q_EMIT openEditorShortcutChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setPreviousLayoutShortcut(const QString& shortcut)
{
    if (m_previousLayoutShortcut != shortcut) {
        m_previousLayoutShortcut = shortcut;
        Q_EMIT previousLayoutShortcutChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setNextLayoutShortcut(const QString& shortcut)
{
    if (m_nextLayoutShortcut != shortcut) {
        m_nextLayoutShortcut = shortcut;
        Q_EMIT nextLayoutShortcutChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setQuickLayout1Shortcut(const QString& shortcut)
{
    setQuickLayoutShortcut(0, shortcut);
}
void Settings::setQuickLayout2Shortcut(const QString& shortcut)
{
    setQuickLayoutShortcut(1, shortcut);
}
void Settings::setQuickLayout3Shortcut(const QString& shortcut)
{
    setQuickLayoutShortcut(2, shortcut);
}
void Settings::setQuickLayout4Shortcut(const QString& shortcut)
{
    setQuickLayoutShortcut(3, shortcut);
}
void Settings::setQuickLayout5Shortcut(const QString& shortcut)
{
    setQuickLayoutShortcut(4, shortcut);
}
void Settings::setQuickLayout6Shortcut(const QString& shortcut)
{
    setQuickLayoutShortcut(5, shortcut);
}
void Settings::setQuickLayout7Shortcut(const QString& shortcut)
{
    setQuickLayoutShortcut(6, shortcut);
}
void Settings::setQuickLayout8Shortcut(const QString& shortcut)
{
    setQuickLayoutShortcut(7, shortcut);
}
void Settings::setQuickLayout9Shortcut(const QString& shortcut)
{
    setQuickLayoutShortcut(8, shortcut);
}

QString Settings::quickLayoutShortcut(int index) const
{
    if (index >= 0 && index < 9) {
        return m_quickLayoutShortcuts[index];
    }
    return QString();
}

void Settings::setQuickLayoutShortcut(int index, const QString& shortcut)
{
    if (index >= 0 && index < 9 && m_quickLayoutShortcuts[index] != shortcut) {
        m_quickLayoutShortcuts[index] = shortcut;
        // Emit the appropriate signal based on index
        switch (index) {
        case 0:
            Q_EMIT quickLayout1ShortcutChanged();
            break;
        case 1:
            Q_EMIT quickLayout2ShortcutChanged();
            break;
        case 2:
            Q_EMIT quickLayout3ShortcutChanged();
            break;
        case 3:
            Q_EMIT quickLayout4ShortcutChanged();
            break;
        case 4:
            Q_EMIT quickLayout5ShortcutChanged();
            break;
        case 5:
            Q_EMIT quickLayout6ShortcutChanged();
            break;
        case 6:
            Q_EMIT quickLayout7ShortcutChanged();
            break;
        case 7:
            Q_EMIT quickLayout8ShortcutChanged();
            break;
        case 8:
            Q_EMIT quickLayout9ShortcutChanged();
            break;
        }
        Q_EMIT settingsChanged();
    }
}

// Keyboard Navigation Shortcuts implementations
void Settings::setMoveWindowLeftShortcut(const QString& shortcut)
{
    if (m_moveWindowLeftShortcut != shortcut) {
        m_moveWindowLeftShortcut = shortcut;
        Q_EMIT moveWindowLeftShortcutChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setMoveWindowRightShortcut(const QString& shortcut)
{
    if (m_moveWindowRightShortcut != shortcut) {
        m_moveWindowRightShortcut = shortcut;
        Q_EMIT moveWindowRightShortcutChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setMoveWindowUpShortcut(const QString& shortcut)
{
    if (m_moveWindowUpShortcut != shortcut) {
        m_moveWindowUpShortcut = shortcut;
        Q_EMIT moveWindowUpShortcutChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setMoveWindowDownShortcut(const QString& shortcut)
{
    if (m_moveWindowDownShortcut != shortcut) {
        m_moveWindowDownShortcut = shortcut;
        Q_EMIT moveWindowDownShortcutChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setFocusZoneLeftShortcut(const QString& shortcut)
{
    if (m_focusZoneLeftShortcut != shortcut) {
        m_focusZoneLeftShortcut = shortcut;
        Q_EMIT focusZoneLeftShortcutChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setFocusZoneRightShortcut(const QString& shortcut)
{
    if (m_focusZoneRightShortcut != shortcut) {
        m_focusZoneRightShortcut = shortcut;
        Q_EMIT focusZoneRightShortcutChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setFocusZoneUpShortcut(const QString& shortcut)
{
    if (m_focusZoneUpShortcut != shortcut) {
        m_focusZoneUpShortcut = shortcut;
        Q_EMIT focusZoneUpShortcutChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setFocusZoneDownShortcut(const QString& shortcut)
{
    if (m_focusZoneDownShortcut != shortcut) {
        m_focusZoneDownShortcut = shortcut;
        Q_EMIT focusZoneDownShortcutChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setPushToEmptyZoneShortcut(const QString& shortcut)
{
    if (m_pushToEmptyZoneShortcut != shortcut) {
        m_pushToEmptyZoneShortcut = shortcut;
        Q_EMIT pushToEmptyZoneShortcutChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setRestoreWindowSizeShortcut(const QString& shortcut)
{
    if (m_restoreWindowSizeShortcut != shortcut) {
        m_restoreWindowSizeShortcut = shortcut;
        Q_EMIT restoreWindowSizeShortcutChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setToggleWindowFloatShortcut(const QString& shortcut)
{
    if (m_toggleWindowFloatShortcut != shortcut) {
        m_toggleWindowFloatShortcut = shortcut;
        Q_EMIT toggleWindowFloatShortcutChanged();
        Q_EMIT settingsChanged();
    }
}

// Swap Window Shortcuts
void Settings::setSwapWindowLeftShortcut(const QString& shortcut)
{
    if (m_swapWindowLeftShortcut != shortcut) {
        m_swapWindowLeftShortcut = shortcut;
        Q_EMIT swapWindowLeftShortcutChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setSwapWindowRightShortcut(const QString& shortcut)
{
    if (m_swapWindowRightShortcut != shortcut) {
        m_swapWindowRightShortcut = shortcut;
        Q_EMIT swapWindowRightShortcutChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setSwapWindowUpShortcut(const QString& shortcut)
{
    if (m_swapWindowUpShortcut != shortcut) {
        m_swapWindowUpShortcut = shortcut;
        Q_EMIT swapWindowUpShortcutChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setSwapWindowDownShortcut(const QString& shortcut)
{
    if (m_swapWindowDownShortcut != shortcut) {
        m_swapWindowDownShortcut = shortcut;
        Q_EMIT swapWindowDownShortcutChanged();
        Q_EMIT settingsChanged();
    }
}

// Snap to Zone by Number Shortcuts
QString Settings::snapToZoneShortcut(int index) const
{
    if (index >= 0 && index < 9) {
        return m_snapToZoneShortcuts[index];
    }
    return QString();
}

void Settings::setSnapToZoneShortcut(int index, const QString& shortcut)
{
    if (index >= 0 && index < 9 && m_snapToZoneShortcuts[index] != shortcut) {
        m_snapToZoneShortcuts[index] = shortcut;
        switch (index) {
        case 0:
            Q_EMIT snapToZone1ShortcutChanged();
            break;
        case 1:
            Q_EMIT snapToZone2ShortcutChanged();
            break;
        case 2:
            Q_EMIT snapToZone3ShortcutChanged();
            break;
        case 3:
            Q_EMIT snapToZone4ShortcutChanged();
            break;
        case 4:
            Q_EMIT snapToZone5ShortcutChanged();
            break;
        case 5:
            Q_EMIT snapToZone6ShortcutChanged();
            break;
        case 6:
            Q_EMIT snapToZone7ShortcutChanged();
            break;
        case 7:
            Q_EMIT snapToZone8ShortcutChanged();
            break;
        case 8:
            Q_EMIT snapToZone9ShortcutChanged();
            break;
        }
        Q_EMIT settingsChanged();
    }
}

void Settings::setSnapToZone1Shortcut(const QString& shortcut)
{
    setSnapToZoneShortcut(0, shortcut);
}

void Settings::setSnapToZone2Shortcut(const QString& shortcut)
{
    setSnapToZoneShortcut(1, shortcut);
}

void Settings::setSnapToZone3Shortcut(const QString& shortcut)
{
    setSnapToZoneShortcut(2, shortcut);
}

void Settings::setSnapToZone4Shortcut(const QString& shortcut)
{
    setSnapToZoneShortcut(3, shortcut);
}

void Settings::setSnapToZone5Shortcut(const QString& shortcut)
{
    setSnapToZoneShortcut(4, shortcut);
}

void Settings::setSnapToZone6Shortcut(const QString& shortcut)
{
    setSnapToZoneShortcut(5, shortcut);
}

void Settings::setSnapToZone7Shortcut(const QString& shortcut)
{
    setSnapToZoneShortcut(6, shortcut);
}

void Settings::setSnapToZone8Shortcut(const QString& shortcut)
{
    setSnapToZoneShortcut(7, shortcut);
}

void Settings::setSnapToZone9Shortcut(const QString& shortcut)
{
    setSnapToZoneShortcut(8, shortcut);
}

// Rotate Windows Shortcuts
void Settings::setRotateWindowsClockwiseShortcut(const QString& shortcut)
{
    if (m_rotateWindowsClockwiseShortcut != shortcut) {
        m_rotateWindowsClockwiseShortcut = shortcut;
        Q_EMIT rotateWindowsClockwiseShortcutChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setRotateWindowsCounterclockwiseShortcut(const QString& shortcut)
{
    if (m_rotateWindowsCounterclockwiseShortcut != shortcut) {
        m_rotateWindowsCounterclockwiseShortcut = shortcut;
        Q_EMIT rotateWindowsCounterclockwiseShortcutChanged();
        Q_EMIT settingsChanged();
    }
}

// Cycle Windows in Zone Shortcuts
void Settings::setCycleWindowForwardShortcut(const QString& shortcut)
{
    if (m_cycleWindowForwardShortcut != shortcut) {
        m_cycleWindowForwardShortcut = shortcut;
        Q_EMIT cycleWindowForwardShortcutChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setCycleWindowBackwardShortcut(const QString& shortcut)
{
    if (m_cycleWindowBackwardShortcut != shortcut) {
        m_cycleWindowBackwardShortcut = shortcut;
        Q_EMIT cycleWindowBackwardShortcutChanged();
        Q_EMIT settingsChanged();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Autotiling Settings
// ═══════════════════════════════════════════════════════════════════════════════

void Settings::setAutotileEnabled(bool enabled)
{
    if (m_autotileEnabled != enabled) {
        m_autotileEnabled = enabled;
        Q_EMIT autotileEnabledChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setAutotileAlgorithm(const QString& algorithm)
{
    // Validate against known algorithms
    static const QStringList validAlgorithms = {
        DBus::AutotileAlgorithm::MasterStack,
        DBus::AutotileAlgorithm::BSP,
        DBus::AutotileAlgorithm::Columns,
        DBus::AutotileAlgorithm::Rows,
        DBus::AutotileAlgorithm::Fibonacci,
        DBus::AutotileAlgorithm::Monocle,
        DBus::AutotileAlgorithm::ThreeColumn
    };

    QString validatedAlgorithm = algorithm;
    if (!validAlgorithms.contains(algorithm)) {
        qCWarning(lcConfig) << "Unknown autotile algorithm:" << algorithm
                            << "- defaulting to master-stack";
        validatedAlgorithm = DBus::AutotileAlgorithm::MasterStack;
    }

    if (m_autotileAlgorithm != validatedAlgorithm) {
        m_autotileAlgorithm = validatedAlgorithm;
        Q_EMIT autotileAlgorithmChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setAutotileSplitRatio(qreal ratio)
{
    ratio = qBound(AutotileDefaults::MinSplitRatio, ratio, AutotileDefaults::MaxSplitRatio);
    // Use qFuzzyCompare properly for values that could be near zero
    if (!qFuzzyCompare(1.0 + m_autotileSplitRatio, 1.0 + ratio)) {
        m_autotileSplitRatio = ratio;
        Q_EMIT autotileSplitRatioChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setAutotileMasterCount(int count)
{
    count = qBound(AutotileDefaults::MinMasterCount, count, AutotileDefaults::MaxMasterCount);
    if (m_autotileMasterCount != count) {
        m_autotileMasterCount = count;
        Q_EMIT autotileMasterCountChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setAutotileInnerGap(int gap)
{
    gap = qBound(AutotileDefaults::MinGap, gap, AutotileDefaults::MaxGap);
    if (m_autotileInnerGap != gap) {
        m_autotileInnerGap = gap;
        Q_EMIT autotileInnerGapChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setAutotileOuterGap(int gap)
{
    gap = qBound(AutotileDefaults::MinGap, gap, AutotileDefaults::MaxGap);
    if (m_autotileOuterGap != gap) {
        m_autotileOuterGap = gap;
        Q_EMIT autotileOuterGapChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setAutotileFocusNewWindows(bool focus)
{
    if (m_autotileFocusNewWindows != focus) {
        m_autotileFocusNewWindows = focus;
        Q_EMIT autotileFocusNewWindowsChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setAutotileSmartGaps(bool smart)
{
    if (m_autotileSmartGaps != smart) {
        m_autotileSmartGaps = smart;
        Q_EMIT autotileSmartGapsChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setAutotileInsertPosition(AutotileInsertPosition position)
{
    if (m_autotileInsertPosition != position) {
        m_autotileInsertPosition = position;
        Q_EMIT autotileInsertPositionChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setAutotileInsertPositionInt(int position)
{
    if (position >= 0 && position <= 2) {
        setAutotileInsertPosition(static_cast<AutotileInsertPosition>(position));
    }
}

void Settings::setAutotileToggleShortcut(const QString& shortcut)
{
    if (m_autotileToggleShortcut != shortcut) {
        m_autotileToggleShortcut = shortcut;
        Q_EMIT autotileToggleShortcutChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setAutotileFocusMasterShortcut(const QString& shortcut)
{
    if (m_autotileFocusMasterShortcut != shortcut) {
        m_autotileFocusMasterShortcut = shortcut;
        Q_EMIT autotileFocusMasterShortcutChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setAutotileSwapMasterShortcut(const QString& shortcut)
{
    if (m_autotileSwapMasterShortcut != shortcut) {
        m_autotileSwapMasterShortcut = shortcut;
        Q_EMIT autotileSwapMasterShortcutChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setAutotileIncMasterRatioShortcut(const QString& shortcut)
{
    if (m_autotileIncMasterRatioShortcut != shortcut) {
        m_autotileIncMasterRatioShortcut = shortcut;
        Q_EMIT autotileIncMasterRatioShortcutChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setAutotileDecMasterRatioShortcut(const QString& shortcut)
{
    if (m_autotileDecMasterRatioShortcut != shortcut) {
        m_autotileDecMasterRatioShortcut = shortcut;
        Q_EMIT autotileDecMasterRatioShortcutChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setAutotileIncMasterCountShortcut(const QString& shortcut)
{
    if (m_autotileIncMasterCountShortcut != shortcut) {
        m_autotileIncMasterCountShortcut = shortcut;
        Q_EMIT autotileIncMasterCountShortcutChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setAutotileDecMasterCountShortcut(const QString& shortcut)
{
    if (m_autotileDecMasterCountShortcut != shortcut) {
        m_autotileDecMasterCountShortcut = shortcut;
        Q_EMIT autotileDecMasterCountShortcutChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setAutotileRetileShortcut(const QString& shortcut)
{
    if (m_autotileRetileShortcut != shortcut) {
        m_autotileRetileShortcut = shortcut;
        Q_EMIT autotileRetileShortcutChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setAutotileAnimationsEnabled(bool enabled)
{
    if (m_autotileAnimationsEnabled != enabled) {
        m_autotileAnimationsEnabled = enabled;
        Q_EMIT autotileAnimationsEnabledChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setAutotileAnimationDuration(int duration)
{
    // Clamp to reasonable range (50-500ms)
    duration = qBound(50, duration, 500);
    if (m_autotileAnimationDuration != duration) {
        m_autotileAnimationDuration = duration;
        Q_EMIT autotileAnimationDurationChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setAutotileFocusFollowsMouse(bool focus)
{
    if (m_autotileFocusFollowsMouse != focus) {
        m_autotileFocusFollowsMouse = focus;
        Q_EMIT autotileFocusFollowsMouseChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setAutotileRespectMinimumSize(bool respect)
{
    if (m_autotileRespectMinimumSize != respect) {
        m_autotileRespectMinimumSize = respect;
        Q_EMIT autotileRespectMinimumSizeChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setAutotileShowActiveBorder(bool show)
{
    if (m_autotileShowActiveBorder != show) {
        m_autotileShowActiveBorder = show;
        Q_EMIT autotileShowActiveBorderChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setAutotileActiveBorderWidth(int width)
{
    width = qBound(1, width, 10);
    if (m_autotileActiveBorderWidth != width) {
        m_autotileActiveBorderWidth = width;
        Q_EMIT autotileActiveBorderWidthChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setAutotileUseSystemBorderColor(bool use)
{
    if (m_autotileUseSystemBorderColor != use) {
        m_autotileUseSystemBorderColor = use;
        Q_EMIT autotileUseSystemBorderColorChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setAutotileActiveBorderColor(const QColor& color)
{
    if (m_autotileActiveBorderColor != color) {
        m_autotileActiveBorderColor = color;
        Q_EMIT autotileActiveBorderColorChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setAutotileMonocleHideOthers(bool hide)
{
    if (m_autotileMonocleHideOthers != hide) {
        m_autotileMonocleHideOthers = hide;
        Q_EMIT autotileMonocleHideOthersChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::setAutotileMonocleShowTabs(bool show)
{
    if (m_autotileMonocleShowTabs != show) {
        m_autotileMonocleShowTabs = show;
        Q_EMIT autotileMonocleShowTabsChanged();
        Q_EMIT settingsChanged();
    }
}

bool Settings::isWindowExcluded(const QString& appName, const QString& windowClass) const
{
    for (const auto& excluded : m_excludedApplications) {
        if (appName.contains(excluded, Qt::CaseInsensitive)) {
            return true;
        }
    }

    for (const auto& excluded : m_excludedWindowClasses) {
        if (windowClass.contains(excluded, Qt::CaseInsensitive)) {
            return true;
        }
    }

    return false;
}

void Settings::load()
{
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
    KConfigGroup activation = config->group(QStringLiteral("Activation"));
    KConfigGroup display = config->group(QStringLiteral("Display"));
    KConfigGroup appearance = config->group(QStringLiteral("Appearance"));
    KConfigGroup zones = config->group(QStringLiteral("Zones"));
    KConfigGroup behavior = config->group(QStringLiteral("Behavior"));
    KConfigGroup exclusions = config->group(QStringLiteral("Exclusions"));

    // Activation with validation (defaults from .kcfg via ConfigDefaults)
    m_shiftDragToActivate = activation.readEntry("ShiftDrag", ConfigDefaults::shiftDrag()); // Deprecated

    // New modifier settings with migration from old boolean
    int dragMod = activation.readEntry("DragActivationModifier", -1);
    if (dragMod == -1) {
        if (activation.hasKey("ShiftDrag")) {
            // Migrate from old shiftDragToActivate setting
            if (m_shiftDragToActivate) {
                m_dragActivationModifier = DragModifier::Shift;
            } else {
                m_dragActivationModifier = static_cast<DragModifier>(ConfigDefaults::dragActivationModifier());
                qCDebug(lcConfig)
                    << "Migrated ShiftDrag=false to DragActivationModifier=Alt (was causing login overlay issue)";
            }
            activation.writeEntry("DragActivationModifier", static_cast<int>(m_dragActivationModifier));
        } else {
            // New install: use default from .kcfg
            m_dragActivationModifier = static_cast<DragModifier>(ConfigDefaults::dragActivationModifier());
            activation.writeEntry("DragActivationModifier", static_cast<int>(m_dragActivationModifier));
        }
    } else {
        m_dragActivationModifier =
            static_cast<DragModifier>(qBound(0, dragMod, static_cast<int>(DragModifier::CtrlAltMeta)));
    }
    qCDebug(lcConfig) << "Loaded DragActivationModifier=" << static_cast<int>(m_dragActivationModifier);

    // Skip-snap modifier: hold this to move window without snapping
    int skipMod = activation.readEntry("SkipSnapModifier", ConfigDefaults::skipSnapModifier());
    m_skipSnapModifier = static_cast<DragModifier>(qBound(0, skipMod, 8));

    // Multi-zone modifier: hold this to span windows across multiple zones
    int multiZoneMod = activation.readEntry("MultiZoneModifier", ConfigDefaults::multiZoneModifier());
    if (multiZoneMod < 0 || multiZoneMod > static_cast<int>(DragModifier::CtrlAltMeta)) {
        qCWarning(lcConfig) << "Invalid MultiZoneModifier value:" << multiZoneMod << "using default";
        multiZoneMod = ConfigDefaults::multiZoneModifier();
    }
    m_multiZoneModifier = static_cast<DragModifier>(multiZoneMod);
    qCDebug(lcConfig) << "Loaded MultiZoneModifier=" << multiZoneMod;

    m_middleClickMultiZone = activation.readEntry("MiddleClickMultiZone", ConfigDefaults::middleClickMultiZone());

    // Display (defaults from .kcfg via ConfigDefaults)
    m_showZonesOnAllMonitors = display.readEntry("ShowOnAllMonitors", ConfigDefaults::showOnAllMonitors());
    m_disabledMonitors = display.readEntry("DisabledMonitors", QStringList());
    m_showZoneNumbers = display.readEntry("ShowNumbers", ConfigDefaults::showNumbers());
    m_flashZonesOnSwitch = display.readEntry("FlashOnSwitch", ConfigDefaults::flashOnSwitch());
    m_showOsdOnLayoutSwitch = display.readEntry("ShowOsdOnLayoutSwitch", ConfigDefaults::showOsdOnLayoutSwitch());
    m_showNavigationOsd = display.readEntry("ShowNavigationOsd", ConfigDefaults::showNavigationOsd());
    int osdStyleInt = display.readEntry("OsdStyle", ConfigDefaults::osdStyle());
    if (osdStyleInt < 0 || osdStyleInt > 2) {
        qCWarning(lcConfig) << "Invalid OSD style:" << osdStyleInt << "using default";
        osdStyleInt = ConfigDefaults::osdStyle();
    }
    m_osdStyle = static_cast<OsdStyle>(osdStyleInt);

    // Appearance with validation (defaults from .kcfg via ConfigDefaults)
    m_useSystemColors = appearance.readEntry("UseSystemColors", ConfigDefaults::useSystemColors());

    // Validate colors
    QColor highlightColor = appearance.readEntry("HighlightColor", ConfigDefaults::highlightColor());
    if (!highlightColor.isValid()) {
        qCWarning(lcConfig) << "Invalid highlight color, using default";
        highlightColor = ConfigDefaults::highlightColor();
    }
    m_highlightColor = highlightColor;

    QColor inactiveColor = appearance.readEntry("InactiveColor", ConfigDefaults::inactiveColor());
    if (!inactiveColor.isValid()) {
        qCWarning(lcConfig) << "Invalid inactive color, using default";
        inactiveColor = ConfigDefaults::inactiveColor();
    }
    m_inactiveColor = inactiveColor;

    QColor borderColor = appearance.readEntry("BorderColor", ConfigDefaults::borderColor());
    if (!borderColor.isValid()) {
        qCWarning(lcConfig) << "Invalid border color, using default";
        borderColor = ConfigDefaults::borderColor();
    }
    m_borderColor = borderColor;

    QColor numberColor = appearance.readEntry("NumberColor", ConfigDefaults::numberColor());
    if (!numberColor.isValid()) {
        qCWarning(lcConfig) << "Invalid number color, using default";
        numberColor = ConfigDefaults::numberColor();
    }
    m_numberColor = numberColor;

    // Validate opacity (0.0 to 1.0)
    qreal activeOpacity = appearance.readEntry("ActiveOpacity", ConfigDefaults::activeOpacity());
    if (activeOpacity < 0.0 || activeOpacity > 1.0) {
        qCWarning(lcConfig) << "Invalid active opacity:" << activeOpacity << "clamping to valid range";
        activeOpacity = qBound(0.0, activeOpacity, 1.0);
    }
    m_activeOpacity = activeOpacity;

    qreal inactiveOpacity = appearance.readEntry("InactiveOpacity", ConfigDefaults::inactiveOpacity());
    if (inactiveOpacity < 0.0 || inactiveOpacity > 1.0) {
        qCWarning(lcConfig) << "Invalid inactive opacity:" << inactiveOpacity << "clamping to valid range";
        inactiveOpacity = qBound(0.0, inactiveOpacity, 1.0);
    }
    m_inactiveOpacity = inactiveOpacity;

    // Validate dimensions (non-negative)
    int borderWidth = appearance.readEntry("BorderWidth", ConfigDefaults::borderWidth());
    if (borderWidth < 0) {
        qCWarning(lcConfig) << "Invalid border width:" << borderWidth << "using default";
        borderWidth = ConfigDefaults::borderWidth();
    }
    m_borderWidth = borderWidth;

    int borderRadius = appearance.readEntry("BorderRadius", ConfigDefaults::borderRadius());
    if (borderRadius < 0) {
        qCWarning(lcConfig) << "Invalid border radius:" << borderRadius << "using default";
        borderRadius = ConfigDefaults::borderRadius();
    }
    m_borderRadius = borderRadius;

    m_enableBlur = appearance.readEntry("EnableBlur", ConfigDefaults::enableBlur());

    // Zones with validation (defaults from .kcfg via ConfigDefaults)
    int zonePadding = zones.readEntry("Padding", ConfigDefaults::zonePadding());
    if (zonePadding < 0) {
        qCWarning(lcConfig) << "Invalid zone padding:" << zonePadding << "using default";
        zonePadding = ConfigDefaults::zonePadding();
    }
    m_zonePadding = zonePadding;

    int outerGap = zones.readEntry("OuterGap", ConfigDefaults::outerGap());
    if (outerGap < 0) {
        qCWarning(lcConfig) << "Invalid outer gap:" << outerGap << "using default";
        outerGap = ConfigDefaults::outerGap();
    }
    m_outerGap = outerGap;

    int adjacentThreshold = zones.readEntry("AdjacentThreshold", ConfigDefaults::adjacentThreshold());
    if (adjacentThreshold < 0) {
        qCWarning(lcConfig) << "Invalid adjacent threshold:" << adjacentThreshold << "using default";
        adjacentThreshold = ConfigDefaults::adjacentThreshold();
    }
    m_adjacentThreshold = adjacentThreshold;

    // Performance and behavior settings with validation (defaults from .kcfg via ConfigDefaults)
    int pollIntervalMs = zones.readEntry("PollIntervalMs", ConfigDefaults::pollIntervalMs());
    if (pollIntervalMs < 10 || pollIntervalMs > 1000) {
        qCWarning(lcConfig) << "Invalid poll interval:" << pollIntervalMs << "using default (must be 10-1000ms)";
        pollIntervalMs = ConfigDefaults::pollIntervalMs();
    }
    m_pollIntervalMs = pollIntervalMs;

    int minimumZoneSizePx = zones.readEntry("MinimumZoneSizePx", ConfigDefaults::minimumZoneSizePx());
    if (minimumZoneSizePx < 50 || minimumZoneSizePx > 500) {
        qCWarning(lcConfig) << "Invalid minimum zone size:" << minimumZoneSizePx << "using default (must be 50-500px)";
        minimumZoneSizePx = ConfigDefaults::minimumZoneSizePx();
    }
    m_minimumZoneSizePx = minimumZoneSizePx;

    int minimumZoneDisplaySizePx = zones.readEntry("MinimumZoneDisplaySizePx", ConfigDefaults::minimumZoneDisplaySizePx());
    if (minimumZoneDisplaySizePx < 1 || minimumZoneDisplaySizePx > 50) {
        qCWarning(lcConfig) << "Invalid minimum zone display size:" << minimumZoneDisplaySizePx
                            << "using default (must be 1-50px)";
        minimumZoneDisplaySizePx = ConfigDefaults::minimumZoneDisplaySizePx();
    }
    m_minimumZoneDisplaySizePx = minimumZoneDisplaySizePx;

    // Behavior (defaults from .kcfg via ConfigDefaults)
    m_keepWindowsInZonesOnResolutionChange = behavior.readEntry("KeepOnResolutionChange", ConfigDefaults::keepWindowsInZonesOnResolutionChange());
    m_moveNewWindowsToLastZone = behavior.readEntry("MoveNewToLastZone", ConfigDefaults::moveNewWindowsToLastZone());
    m_restoreOriginalSizeOnUnsnap = behavior.readEntry("RestoreSizeOnUnsnap", ConfigDefaults::restoreOriginalSizeOnUnsnap());
    int stickyHandling =
        behavior.readEntry("StickyWindowHandling", ConfigDefaults::stickyWindowHandling());
    m_stickyWindowHandling =
        static_cast<StickyWindowHandling>(qBound(static_cast<int>(StickyWindowHandling::TreatAsNormal), stickyHandling,
                                                 static_cast<int>(StickyWindowHandling::IgnoreAll)));
    m_restoreWindowsToZonesOnLogin = behavior.readEntry(QLatin1String("RestoreWindowsToZonesOnLogin"), ConfigDefaults::restoreWindowsToZonesOnLogin());
    // Normalize UUID to default format (with braces) for consistent comparison
    // Handles migration from configs saved with WithoutBraces format
    m_defaultLayoutId = normalizeUuidString(behavior.readEntry("DefaultLayoutId", QString()));

    // Exclusions (defaults from .kcfg via ConfigDefaults)
    m_excludedApplications = exclusions.readEntry("Applications", QStringList());
    m_excludedWindowClasses = exclusions.readEntry("WindowClasses", QStringList());
    m_excludeTransientWindows = exclusions.readEntry("ExcludeTransientWindows", ConfigDefaults::excludeTransientWindows());
    int minWidth = exclusions.readEntry("MinimumWindowWidth", ConfigDefaults::minimumWindowWidth());
    m_minimumWindowWidth = qBound(0, minWidth, 2000);  // Match kcfg max for 4K monitors
    int minHeight = exclusions.readEntry("MinimumWindowHeight", ConfigDefaults::minimumWindowHeight());
    m_minimumWindowHeight = qBound(0, minHeight, 2000);  // Match kcfg max for 4K monitors

    // Zone Selector (defaults from .kcfg via ConfigDefaults)
    KConfigGroup zoneSelector = config->group(QStringLiteral("ZoneSelector"));
    m_zoneSelectorEnabled = zoneSelector.readEntry("Enabled", ConfigDefaults::zoneSelectorEnabled());
    int triggerDistance = zoneSelector.readEntry("TriggerDistance", ConfigDefaults::triggerDistance());
    if (triggerDistance < 10 || triggerDistance > 200) {
        qCWarning(lcConfig) << "Invalid zone selector trigger distance:" << triggerDistance
                            << "using default (must be 10-200px)";
        triggerDistance = ConfigDefaults::triggerDistance();
    }
    m_zoneSelectorTriggerDistance = triggerDistance;
    int selectorPos = zoneSelector.readEntry("Position", ConfigDefaults::position());
    // Valid positions are 0-8 except 4 (center)
    if (selectorPos >= 0 && selectorPos <= 8 && selectorPos != 4) {
        m_zoneSelectorPosition = static_cast<ZoneSelectorPosition>(selectorPos);
    } else {
        m_zoneSelectorPosition = static_cast<ZoneSelectorPosition>(ConfigDefaults::position());
    }
    int selectorMode = zoneSelector.readEntry("LayoutMode", ConfigDefaults::layoutMode());
    m_zoneSelectorLayoutMode = static_cast<ZoneSelectorLayoutMode>(
        qBound(0, selectorMode, static_cast<int>(ZoneSelectorLayoutMode::Vertical)));
    int previewWidth = zoneSelector.readEntry("PreviewWidth", ConfigDefaults::previewWidth());
    if (previewWidth < 80 || previewWidth > 400) {
        qCWarning(lcConfig) << "Invalid zone selector preview width:" << previewWidth << "using default (80-400px)";
        previewWidth = ConfigDefaults::previewWidth();
    }
    m_zoneSelectorPreviewWidth = previewWidth;
    int previewHeight = zoneSelector.readEntry("PreviewHeight", ConfigDefaults::previewHeight());
    if (previewHeight < 60 || previewHeight > 300) {
        qCWarning(lcConfig) << "Invalid zone selector preview height:" << previewHeight << "using default (60-300px)";
        previewHeight = ConfigDefaults::previewHeight();
    }
    m_zoneSelectorPreviewHeight = previewHeight;
    m_zoneSelectorPreviewLockAspect = zoneSelector.readEntry("PreviewLockAspect", ConfigDefaults::previewLockAspect());
    int gridColumns = zoneSelector.readEntry("GridColumns", ConfigDefaults::gridColumns());
    if (gridColumns < 1 || gridColumns > 10) {
        qCWarning(lcConfig) << "Invalid zone selector grid columns:" << gridColumns << "using default (1-10)";
        gridColumns = ConfigDefaults::gridColumns();
    }
    m_zoneSelectorGridColumns = gridColumns;

    // Size mode (Auto/Manual)
    int sizeMode = zoneSelector.readEntry("SizeMode", ConfigDefaults::sizeMode());
    m_zoneSelectorSizeMode =
        static_cast<ZoneSelectorSizeMode>(qBound(0, sizeMode, static_cast<int>(ZoneSelectorSizeMode::Manual)));

    // Max visible rows before scrolling (Auto mode)
    int maxRows = zoneSelector.readEntry("MaxRows", ConfigDefaults::maxRows());
    if (maxRows < 1 || maxRows > 10) {
        qCWarning(lcConfig) << "Invalid zone selector max rows:" << maxRows << "using default (1-10)";
        maxRows = ConfigDefaults::maxRows();
    }
    m_zoneSelectorMaxRows = maxRows;

    // Shader Effects (defaults from .kcfg via ConfigDefaults)
    KConfigGroup shaders = config->group(QStringLiteral("Shaders"));
    m_enableShaderEffects = shaders.readEntry("EnableShaderEffects", ConfigDefaults::enableShaderEffects());
    m_shaderFrameRate = qBound(30, shaders.readEntry("ShaderFrameRate", ConfigDefaults::shaderFrameRate()), 144);

    // Global Shortcuts (defaults from .kcfg via ConfigDefaults)
    KConfigGroup globalShortcuts = config->group(QStringLiteral("GlobalShortcuts"));
    m_openEditorShortcut = globalShortcuts.readEntry("OpenEditorShortcut", ConfigDefaults::openEditorShortcut());
    m_previousLayoutShortcut = globalShortcuts.readEntry("PreviousLayoutShortcut", QStringLiteral("Meta+Alt+["));
    m_nextLayoutShortcut = globalShortcuts.readEntry("NextLayoutShortcut", QStringLiteral("Meta+Alt+]"));
    // Quick layout shortcuts - use ConfigDefaults for each
    const QString quickLayoutDefaults[9] = {
        ConfigDefaults::quickLayout1Shortcut(), ConfigDefaults::quickLayout2Shortcut(),
        ConfigDefaults::quickLayout3Shortcut(), ConfigDefaults::quickLayout4Shortcut(),
        ConfigDefaults::quickLayout5Shortcut(), ConfigDefaults::quickLayout6Shortcut(),
        ConfigDefaults::quickLayout7Shortcut(), ConfigDefaults::quickLayout8Shortcut(),
        ConfigDefaults::quickLayout9Shortcut()
    };
    for (int i = 0; i < 9; ++i) {
        QString key = QStringLiteral("QuickLayout%1Shortcut").arg(i + 1);
        m_quickLayoutShortcuts[i] = globalShortcuts.readEntry(key, quickLayoutDefaults[i]);
    }

    // Keyboard Navigation Shortcuts (defaults from .kcfg via ConfigDefaults)
    // Shortcut pattern philosophy for consistency and KDE conflict avoidance:
    //   Meta+Alt+{key}         = Layout operations ([, ], 1-9, Return, Escape, F)
    //   Meta+Alt+Shift+Arrow   = Window zone movement
    //   Alt+Shift+Arrow        = Focus zone navigation (lighter action, no Meta)
    //   Meta+Ctrl+{1-9}        = Direct zone snapping
    // Meta+Shift+Left/Right conflicts with KDE's "Window to Next/Previous Screen";
    // we use Meta+Alt+Shift+Arrow instead.
    KConfigGroup navigationShortcuts = config->group(QStringLiteral("NavigationShortcuts"));
    m_moveWindowLeftShortcut = navigationShortcuts.readEntry("MoveWindowLeft", ConfigDefaults::moveWindowLeftShortcut());
    m_moveWindowRightShortcut =
        navigationShortcuts.readEntry("MoveWindowRight", ConfigDefaults::moveWindowRightShortcut());
    m_moveWindowUpShortcut = navigationShortcuts.readEntry("MoveWindowUp", ConfigDefaults::moveWindowUpShortcut());
    m_moveWindowDownShortcut = navigationShortcuts.readEntry("MoveWindowDown", ConfigDefaults::moveWindowDownShortcut());
    // Meta+Arrow conflicts with KDE's Quick Tile; we use Alt+Shift+Arrow instead.
    m_focusZoneLeftShortcut = navigationShortcuts.readEntry("FocusZoneLeft", ConfigDefaults::focusZoneLeftShortcut());
    m_focusZoneRightShortcut = navigationShortcuts.readEntry("FocusZoneRight", ConfigDefaults::focusZoneRightShortcut());
    m_focusZoneUpShortcut = navigationShortcuts.readEntry("FocusZoneUp", ConfigDefaults::focusZoneUpShortcut());
    m_focusZoneDownShortcut = navigationShortcuts.readEntry("FocusZoneDown", ConfigDefaults::focusZoneDownShortcut());
    m_pushToEmptyZoneShortcut = navigationShortcuts.readEntry("PushToEmptyZone", ConfigDefaults::pushToEmptyZoneShortcut());
    m_restoreWindowSizeShortcut = navigationShortcuts.readEntry("RestoreWindowSize", ConfigDefaults::restoreWindowSizeShortcut());
    m_toggleWindowFloatShortcut = navigationShortcuts.readEntry("ToggleWindowFloat", ConfigDefaults::toggleWindowFloatShortcut());

    // Swap Window Shortcuts (Meta+Ctrl+Alt+Arrow)
    // Meta+Ctrl+Arrow conflicts with KDE's virtual desktop switching;
    // we add Alt to make Meta+Ctrl+Alt+Arrow for swap operations.
    m_swapWindowLeftShortcut =
        navigationShortcuts.readEntry("SwapWindowLeft", ConfigDefaults::swapWindowLeftShortcut());
    m_swapWindowRightShortcut =
        navigationShortcuts.readEntry("SwapWindowRight", ConfigDefaults::swapWindowRightShortcut());
    m_swapWindowUpShortcut = navigationShortcuts.readEntry("SwapWindowUp", ConfigDefaults::swapWindowUpShortcut());
    m_swapWindowDownShortcut = navigationShortcuts.readEntry("SwapWindowDown", ConfigDefaults::swapWindowDownShortcut());

    // Snap to Zone by Number Shortcuts (Meta+Ctrl+1-9) - using ConfigDefaults
    const QString snapToZoneDefaults[9] = {
        ConfigDefaults::snapToZone1Shortcut(), ConfigDefaults::snapToZone2Shortcut(),
        ConfigDefaults::snapToZone3Shortcut(), ConfigDefaults::snapToZone4Shortcut(),
        ConfigDefaults::snapToZone5Shortcut(), ConfigDefaults::snapToZone6Shortcut(),
        ConfigDefaults::snapToZone7Shortcut(), ConfigDefaults::snapToZone8Shortcut(),
        ConfigDefaults::snapToZone9Shortcut()
    };
    for (int i = 0; i < 9; ++i) {
        QString key = QStringLiteral("SnapToZone%1").arg(i + 1);
        m_snapToZoneShortcuts[i] = navigationShortcuts.readEntry(key, snapToZoneDefaults[i]);
    }

    // Rotate Windows Shortcuts (Meta+Ctrl+[ / Meta+Ctrl+])
    // Rotates all windows in the current layout clockwise or counterclockwise
    m_rotateWindowsClockwiseShortcut =
        navigationShortcuts.readEntry("RotateWindowsClockwise", ConfigDefaults::rotateWindowsClockwiseShortcut());
    m_rotateWindowsCounterclockwiseShortcut =
        navigationShortcuts.readEntry("RotateWindowsCounterclockwise", ConfigDefaults::rotateWindowsCounterclockwiseShortcut());

    // Cycle Windows in Zone Shortcuts (Meta+Alt+. / Meta+Alt+,)
    // Cycles focus between windows stacked in the same zone (monocle-style navigation)
    m_cycleWindowForwardShortcut =
        navigationShortcuts.readEntry("CycleWindowForward", ConfigDefaults::cycleWindowForwardShortcut());
    m_cycleWindowBackwardShortcut =
        navigationShortcuts.readEntry("CycleWindowBackward", ConfigDefaults::cycleWindowBackwardShortcut());

    // ═══════════════════════════════════════════════════════════════════════════
    // Autotiling Settings (defaults from .kcfg via ConfigDefaults)
    // Note: AutotileDefaults in constants.h still used for min/max bounds
    // ═══════════════════════════════════════════════════════════════════════════
    KConfigGroup autotiling = config->group(QStringLiteral("Autotiling"));

    m_autotileEnabled = autotiling.readEntry("AutotileEnabled", ConfigDefaults::autotileEnabled());
    m_autotileAlgorithm = autotiling.readEntry("AutotileAlgorithm", ConfigDefaults::autotileAlgorithm());

    qreal splitRatio = autotiling.readEntry("AutotileSplitRatio", ConfigDefaults::autotileSplitRatio());
    if (splitRatio < AutotileDefaults::MinSplitRatio || splitRatio > AutotileDefaults::MaxSplitRatio) {
        qCWarning(lcConfig) << "Invalid autotile split ratio:" << splitRatio << "clamping to valid range";
        splitRatio = qBound(AutotileDefaults::MinSplitRatio, splitRatio, AutotileDefaults::MaxSplitRatio);
    }
    m_autotileSplitRatio = splitRatio;

    int masterCount = autotiling.readEntry("AutotileMasterCount", ConfigDefaults::autotileMasterCount());
    if (masterCount < AutotileDefaults::MinMasterCount || masterCount > AutotileDefaults::MaxMasterCount) {
        qCWarning(lcConfig) << "Invalid autotile master count:" << masterCount << "clamping to valid range";
        masterCount = qBound(AutotileDefaults::MinMasterCount, masterCount, AutotileDefaults::MaxMasterCount);
    }
    m_autotileMasterCount = masterCount;

    int innerGap = autotiling.readEntry("AutotileInnerGap", ConfigDefaults::autotileInnerGap());
    if (innerGap < AutotileDefaults::MinGap || innerGap > AutotileDefaults::MaxGap) {
        qCWarning(lcConfig) << "Invalid autotile inner gap:" << innerGap << "clamping to valid range";
        innerGap = qBound(AutotileDefaults::MinGap, innerGap, AutotileDefaults::MaxGap);
    }
    m_autotileInnerGap = innerGap;

    int autotileOuterGap = autotiling.readEntry("AutotileOuterGap", ConfigDefaults::autotileOuterGap());
    if (autotileOuterGap < AutotileDefaults::MinGap || autotileOuterGap > AutotileDefaults::MaxGap) {
        qCWarning(lcConfig) << "Invalid autotile outer gap:" << autotileOuterGap << "clamping to valid range";
        autotileOuterGap = qBound(AutotileDefaults::MinGap, autotileOuterGap, AutotileDefaults::MaxGap);
    }
    m_autotileOuterGap = autotileOuterGap;

    m_autotileFocusNewWindows = autotiling.readEntry("AutotileFocusNewWindows", ConfigDefaults::autotileFocusNewWindows());
    m_autotileSmartGaps = autotiling.readEntry("AutotileSmartGaps", ConfigDefaults::autotileSmartGaps());

    int insertPos = autotiling.readEntry("AutotileInsertPosition", ConfigDefaults::autotileInsertPosition());
    if (insertPos < 0 || insertPos > 2) {
        qCWarning(lcConfig) << "Invalid autotile insert position:" << insertPos << "using default";
        insertPos = ConfigDefaults::autotileInsertPosition();
    }
    m_autotileInsertPosition = static_cast<AutotileInsertPosition>(insertPos);

    // Autotile Animation Settings
    m_autotileAnimationsEnabled = autotiling.readEntry("AutotileAnimationsEnabled", ConfigDefaults::autotileAnimationsEnabled());
    m_autotileAnimationDuration = qBound(50, autotiling.readEntry("AutotileAnimationDuration", ConfigDefaults::autotileAnimationDuration()), 500);

    // Additional Autotiling Settings
    m_autotileFocusFollowsMouse = autotiling.readEntry("AutotileFocusFollowsMouse", ConfigDefaults::autotileFocusFollowsMouse());
    m_autotileRespectMinimumSize = autotiling.readEntry("AutotileRespectMinimumSize", ConfigDefaults::autotileRespectMinimumSize());
    m_autotileShowActiveBorder = autotiling.readEntry("AutotileShowActiveBorder", ConfigDefaults::autotileShowActiveBorder());

    int activeBorderWidth = autotiling.readEntry("AutotileActiveBorderWidth", ConfigDefaults::autotileActiveBorderWidth());
    if (activeBorderWidth < 1 || activeBorderWidth > 10) {
        qCWarning(lcConfig) << "Invalid autotile active border width:" << activeBorderWidth << "clamping to valid range";
        activeBorderWidth = qBound(1, activeBorderWidth, 10);
    }
    m_autotileActiveBorderWidth = activeBorderWidth;

    m_autotileUseSystemBorderColor = autotiling.readEntry("AutotileUseSystemBorderColor", ConfigDefaults::autotileUseSystemBorderColor());

    // Get system highlight color as default for custom border color
    KColorScheme borderScheme(QPalette::Active, KColorScheme::Selection);
    QColor systemHighlight = borderScheme.background(KColorScheme::ActiveBackground).color();

    QColor activeBorderColor = autotiling.readEntry("AutotileActiveBorderColor", systemHighlight);
    if (!activeBorderColor.isValid()) {
        qCWarning(lcConfig) << "Invalid autotile active border color, using system highlight";
        activeBorderColor = systemHighlight;
    }
    m_autotileActiveBorderColor = activeBorderColor;

    m_autotileMonocleHideOthers = autotiling.readEntry("AutotileMonocleHideOthers", ConfigDefaults::autotileMonocleHideOthers());
    m_autotileMonocleShowTabs = autotiling.readEntry("AutotileMonocleShowTabs", ConfigDefaults::autotileMonocleShowTabs());

    // Autotiling Shortcuts (defaults from .kcfg via ConfigDefaults, Bismuth-compatible)
    KConfigGroup autotileShortcuts = config->group(QStringLiteral("AutotileShortcuts"));
    m_autotileToggleShortcut = autotileShortcuts.readEntry("ToggleShortcut", ConfigDefaults::autotileToggleShortcut());
    m_autotileFocusMasterShortcut = autotileShortcuts.readEntry("FocusMasterShortcut", ConfigDefaults::autotileFocusMasterShortcut());
    m_autotileSwapMasterShortcut = autotileShortcuts.readEntry("SwapMasterShortcut", ConfigDefaults::autotileSwapMasterShortcut());
    m_autotileIncMasterRatioShortcut = autotileShortcuts.readEntry("IncMasterRatioShortcut", ConfigDefaults::autotileIncMasterRatioShortcut());
    m_autotileDecMasterRatioShortcut = autotileShortcuts.readEntry("DecMasterRatioShortcut", ConfigDefaults::autotileDecMasterRatioShortcut());
    m_autotileIncMasterCountShortcut = autotileShortcuts.readEntry("IncMasterCountShortcut", ConfigDefaults::autotileIncMasterCountShortcut());
    m_autotileDecMasterCountShortcut = autotileShortcuts.readEntry("DecMasterCountShortcut", ConfigDefaults::autotileDecMasterCountShortcut());
    m_autotileRetileShortcut = autotileShortcuts.readEntry("RetileShortcut", ConfigDefaults::autotileRetileShortcut());

    // Apply system colors if enabled
    if (m_useSystemColors) {
        applySystemColorScheme();
    }

    qCDebug(lcConfig) << "Settings loaded successfully";

    // Notify listeners so the overlay updates when KCM saves settings.
    Q_EMIT settingsChanged();
}

void Settings::save()
{
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
    KConfigGroup activation = config->group(QStringLiteral("Activation"));
    KConfigGroup display = config->group(QStringLiteral("Display"));
    KConfigGroup appearance = config->group(QStringLiteral("Appearance"));
    KConfigGroup zones = config->group(QStringLiteral("Zones"));
    KConfigGroup behavior = config->group(QStringLiteral("Behavior"));
    KConfigGroup exclusions = config->group(QStringLiteral("Exclusions"));

    // Activation
    activation.writeEntry("ShiftDrag", m_shiftDragToActivate); // Deprecated, kept for compatibility
    activation.writeEntry("DragActivationModifier", static_cast<int>(m_dragActivationModifier));
    activation.writeEntry("SkipSnapModifier", static_cast<int>(m_skipSnapModifier));
    activation.writeEntry("MultiZoneModifier", static_cast<int>(m_multiZoneModifier));
    activation.writeEntry("MiddleClickMultiZone", m_middleClickMultiZone);

    // Display
    display.writeEntry("ShowOnAllMonitors", m_showZonesOnAllMonitors);
    display.writeEntry("DisabledMonitors", m_disabledMonitors);
    display.writeEntry("ShowNumbers", m_showZoneNumbers);
    display.writeEntry("FlashOnSwitch", m_flashZonesOnSwitch);
    display.writeEntry("ShowOsdOnLayoutSwitch", m_showOsdOnLayoutSwitch);
    display.writeEntry("ShowNavigationOsd", m_showNavigationOsd);
    display.writeEntry("OsdStyle", static_cast<int>(m_osdStyle));

    // Appearance
    appearance.writeEntry("UseSystemColors", m_useSystemColors);
    appearance.writeEntry("HighlightColor", m_highlightColor);
    appearance.writeEntry("InactiveColor", m_inactiveColor);
    appearance.writeEntry("BorderColor", m_borderColor);
    appearance.writeEntry("NumberColor", m_numberColor);
    appearance.writeEntry("ActiveOpacity", m_activeOpacity);
    appearance.writeEntry("InactiveOpacity", m_inactiveOpacity);
    appearance.writeEntry("BorderWidth", m_borderWidth);
    appearance.writeEntry("BorderRadius", m_borderRadius);
    appearance.writeEntry("EnableBlur", m_enableBlur);

    // Zones
    zones.writeEntry("Padding", m_zonePadding);
    zones.writeEntry("OuterGap", m_outerGap);
    zones.writeEntry("AdjacentThreshold", m_adjacentThreshold);

    // Performance and behavior
    zones.writeEntry("PollIntervalMs", m_pollIntervalMs);
    zones.writeEntry("MinimumZoneSizePx", m_minimumZoneSizePx);
    zones.writeEntry("MinimumZoneDisplaySizePx", m_minimumZoneDisplaySizePx);

    // Behavior
    behavior.writeEntry("KeepOnResolutionChange", m_keepWindowsInZonesOnResolutionChange);
    behavior.writeEntry("MoveNewToLastZone", m_moveNewWindowsToLastZone);
    behavior.writeEntry("RestoreSizeOnUnsnap", m_restoreOriginalSizeOnUnsnap);
    behavior.writeEntry("StickyWindowHandling", static_cast<int>(m_stickyWindowHandling));
    behavior.writeEntry(QLatin1String("RestoreWindowsToZonesOnLogin"), m_restoreWindowsToZonesOnLogin);
    behavior.writeEntry("DefaultLayoutId", m_defaultLayoutId);

    // Exclusions
    exclusions.writeEntry("Applications", m_excludedApplications);
    exclusions.writeEntry("WindowClasses", m_excludedWindowClasses);
    exclusions.writeEntry("ExcludeTransientWindows", m_excludeTransientWindows);
    exclusions.writeEntry("MinimumWindowWidth", m_minimumWindowWidth);
    exclusions.writeEntry("MinimumWindowHeight", m_minimumWindowHeight);

    // Zone Selector
    KConfigGroup zoneSelector = config->group(QStringLiteral("ZoneSelector"));
    zoneSelector.writeEntry("Enabled", m_zoneSelectorEnabled);
    zoneSelector.writeEntry("TriggerDistance", m_zoneSelectorTriggerDistance);
    zoneSelector.writeEntry("Position", static_cast<int>(m_zoneSelectorPosition));
    zoneSelector.writeEntry("LayoutMode", static_cast<int>(m_zoneSelectorLayoutMode));
    zoneSelector.writeEntry("PreviewWidth", m_zoneSelectorPreviewWidth);
    zoneSelector.writeEntry("PreviewHeight", m_zoneSelectorPreviewHeight);
    zoneSelector.writeEntry("PreviewLockAspect", m_zoneSelectorPreviewLockAspect);
    zoneSelector.writeEntry("GridColumns", m_zoneSelectorGridColumns);
    zoneSelector.writeEntry("SizeMode", static_cast<int>(m_zoneSelectorSizeMode));
    zoneSelector.writeEntry("MaxRows", m_zoneSelectorMaxRows);

    // Shader Effects
    KConfigGroup shaders = config->group(QStringLiteral("Shaders"));
    shaders.writeEntry("EnableShaderEffects", m_enableShaderEffects);
    shaders.writeEntry("ShaderFrameRate", m_shaderFrameRate);

    // Global Shortcuts
    KConfigGroup globalShortcuts = config->group(QStringLiteral("GlobalShortcuts"));
    globalShortcuts.writeEntry("OpenEditorShortcut", m_openEditorShortcut);
    globalShortcuts.writeEntry("PreviousLayoutShortcut", m_previousLayoutShortcut);
    globalShortcuts.writeEntry("NextLayoutShortcut", m_nextLayoutShortcut);
    for (int i = 0; i < 9; ++i) {
        QString key = QStringLiteral("QuickLayout%1Shortcut").arg(i + 1);
        globalShortcuts.writeEntry(key, m_quickLayoutShortcuts[i]);
    }

    // Keyboard Navigation Shortcuts (Phase 1 features)
    KConfigGroup navigationShortcuts = config->group(QStringLiteral("NavigationShortcuts"));
    navigationShortcuts.writeEntry("MoveWindowLeft", m_moveWindowLeftShortcut);
    navigationShortcuts.writeEntry("MoveWindowRight", m_moveWindowRightShortcut);
    navigationShortcuts.writeEntry("MoveWindowUp", m_moveWindowUpShortcut);
    navigationShortcuts.writeEntry("MoveWindowDown", m_moveWindowDownShortcut);
    navigationShortcuts.writeEntry("FocusZoneLeft", m_focusZoneLeftShortcut);
    navigationShortcuts.writeEntry("FocusZoneRight", m_focusZoneRightShortcut);
    navigationShortcuts.writeEntry("FocusZoneUp", m_focusZoneUpShortcut);
    navigationShortcuts.writeEntry("FocusZoneDown", m_focusZoneDownShortcut);
    navigationShortcuts.writeEntry("PushToEmptyZone", m_pushToEmptyZoneShortcut);
    navigationShortcuts.writeEntry("RestoreWindowSize", m_restoreWindowSizeShortcut);
    navigationShortcuts.writeEntry("ToggleWindowFloat", m_toggleWindowFloatShortcut);

    // Swap Window Shortcuts
    navigationShortcuts.writeEntry("SwapWindowLeft", m_swapWindowLeftShortcut);
    navigationShortcuts.writeEntry("SwapWindowRight", m_swapWindowRightShortcut);
    navigationShortcuts.writeEntry("SwapWindowUp", m_swapWindowUpShortcut);
    navigationShortcuts.writeEntry("SwapWindowDown", m_swapWindowDownShortcut);

    // Snap to Zone by Number Shortcuts
    for (int i = 0; i < 9; ++i) {
        QString key = QStringLiteral("SnapToZone%1").arg(i + 1);
        navigationShortcuts.writeEntry(key, m_snapToZoneShortcuts[i]);
    }

    // Rotate Windows Shortcuts
    navigationShortcuts.writeEntry("RotateWindowsClockwise", m_rotateWindowsClockwiseShortcut);
    navigationShortcuts.writeEntry("RotateWindowsCounterclockwise", m_rotateWindowsCounterclockwiseShortcut);

    // Cycle Windows in Zone Shortcuts
    navigationShortcuts.writeEntry("CycleWindowForward", m_cycleWindowForwardShortcut);
    navigationShortcuts.writeEntry("CycleWindowBackward", m_cycleWindowBackwardShortcut);

    // ═══════════════════════════════════════════════════════════════════════════
    // Autotiling Settings
    // ═══════════════════════════════════════════════════════════════════════════
    KConfigGroup autotiling = config->group(QStringLiteral("Autotiling"));
    autotiling.writeEntry("AutotileEnabled", m_autotileEnabled);
    autotiling.writeEntry("AutotileAlgorithm", m_autotileAlgorithm);
    autotiling.writeEntry("AutotileSplitRatio", m_autotileSplitRatio);
    autotiling.writeEntry("AutotileMasterCount", m_autotileMasterCount);
    autotiling.writeEntry("AutotileInnerGap", m_autotileInnerGap);
    autotiling.writeEntry("AutotileOuterGap", m_autotileOuterGap);
    autotiling.writeEntry("AutotileFocusNewWindows", m_autotileFocusNewWindows);
    autotiling.writeEntry("AutotileSmartGaps", m_autotileSmartGaps);
    autotiling.writeEntry("AutotileInsertPosition", static_cast<int>(m_autotileInsertPosition));

    // Autotile Animation Settings
    autotiling.writeEntry("AutotileAnimationsEnabled", m_autotileAnimationsEnabled);
    autotiling.writeEntry("AutotileAnimationDuration", m_autotileAnimationDuration);

    // Additional Autotiling Settings
    autotiling.writeEntry("AutotileFocusFollowsMouse", m_autotileFocusFollowsMouse);
    autotiling.writeEntry("AutotileRespectMinimumSize", m_autotileRespectMinimumSize);
    autotiling.writeEntry("AutotileShowActiveBorder", m_autotileShowActiveBorder);
    autotiling.writeEntry("AutotileActiveBorderWidth", m_autotileActiveBorderWidth);
    autotiling.writeEntry("AutotileUseSystemBorderColor", m_autotileUseSystemBorderColor);
    autotiling.writeEntry("AutotileActiveBorderColor", m_autotileActiveBorderColor);
    autotiling.writeEntry("AutotileMonocleHideOthers", m_autotileMonocleHideOthers);
    autotiling.writeEntry("AutotileMonocleShowTabs", m_autotileMonocleShowTabs);

    // Autotiling Shortcuts
    KConfigGroup autotileShortcuts = config->group(QStringLiteral("AutotileShortcuts"));
    autotileShortcuts.writeEntry("ToggleShortcut", m_autotileToggleShortcut);
    autotileShortcuts.writeEntry("FocusMasterShortcut", m_autotileFocusMasterShortcut);
    autotileShortcuts.writeEntry("SwapMasterShortcut", m_autotileSwapMasterShortcut);
    autotileShortcuts.writeEntry("IncMasterRatioShortcut", m_autotileIncMasterRatioShortcut);
    autotileShortcuts.writeEntry("DecMasterRatioShortcut", m_autotileDecMasterRatioShortcut);
    autotileShortcuts.writeEntry("IncMasterCountShortcut", m_autotileIncMasterCountShortcut);
    autotileShortcuts.writeEntry("DecMasterCountShortcut", m_autotileDecMasterCountShortcut);
    autotileShortcuts.writeEntry("RetileShortcut", m_autotileRetileShortcut);

    config->sync();
}

void Settings::reset()
{
    // Reset to default values (from .kcfg via ConfigDefaults)
    // Activation settings
    m_shiftDragToActivate = ConfigDefaults::shiftDrag(); // Deprecated
    m_dragActivationModifier = static_cast<DragModifier>(ConfigDefaults::dragActivationModifier());
    m_skipSnapModifier = static_cast<DragModifier>(ConfigDefaults::skipSnapModifier());
    m_multiZoneModifier = static_cast<DragModifier>(ConfigDefaults::multiZoneModifier());
    m_middleClickMultiZone = ConfigDefaults::middleClickMultiZone();

    // Display settings (from .kcfg via ConfigDefaults)
    m_showZonesOnAllMonitors = ConfigDefaults::showOnAllMonitors();
    m_disabledMonitors.clear();
    m_showZoneNumbers = ConfigDefaults::showNumbers();
    m_flashZonesOnSwitch = ConfigDefaults::flashOnSwitch();
    m_showOsdOnLayoutSwitch = ConfigDefaults::showOsdOnLayoutSwitch();
    m_showNavigationOsd = ConfigDefaults::showNavigationOsd();
    m_osdStyle = static_cast<OsdStyle>(ConfigDefaults::osdStyle());

    // Appearance defaults (from .kcfg via ConfigDefaults)
    m_useSystemColors = ConfigDefaults::useSystemColors();
    m_highlightColor = ConfigDefaults::highlightColor();
    m_inactiveColor = ConfigDefaults::inactiveColor();
    m_borderColor = ConfigDefaults::borderColor();
    m_numberColor = ConfigDefaults::numberColor();
    m_activeOpacity = ConfigDefaults::activeOpacity();
    m_inactiveOpacity = ConfigDefaults::inactiveOpacity();
    m_borderWidth = ConfigDefaults::borderWidth();
    m_borderRadius = ConfigDefaults::borderRadius();
    m_enableBlur = ConfigDefaults::enableBlur();

    // Zone settings defaults (from .kcfg via ConfigDefaults)
    m_zonePadding = ConfigDefaults::zonePadding();
    m_outerGap = ConfigDefaults::outerGap();
    m_adjacentThreshold = ConfigDefaults::adjacentThreshold();

    // Performance and behavior defaults (from .kcfg via ConfigDefaults)
    m_pollIntervalMs = ConfigDefaults::pollIntervalMs();
    m_minimumZoneSizePx = ConfigDefaults::minimumZoneSizePx();
    m_minimumZoneDisplaySizePx = ConfigDefaults::minimumZoneDisplaySizePx();

    m_keepWindowsInZonesOnResolutionChange = ConfigDefaults::keepWindowsInZonesOnResolutionChange();
    m_moveNewWindowsToLastZone = ConfigDefaults::moveNewWindowsToLastZone();
    m_restoreOriginalSizeOnUnsnap = ConfigDefaults::restoreOriginalSizeOnUnsnap();
    m_stickyWindowHandling = static_cast<StickyWindowHandling>(ConfigDefaults::stickyWindowHandling());
    m_restoreWindowsToZonesOnLogin = ConfigDefaults::restoreWindowsToZonesOnLogin();
    m_defaultLayoutId.clear();

    // Exclusions (from .kcfg via ConfigDefaults)
    m_excludedApplications.clear();
    m_excludedWindowClasses.clear();
    m_excludeTransientWindows = ConfigDefaults::excludeTransientWindows();
    m_minimumWindowWidth = ConfigDefaults::minimumWindowWidth();
    m_minimumWindowHeight = ConfigDefaults::minimumWindowHeight();

    // Zone Selector defaults (from .kcfg via ConfigDefaults)
    m_zoneSelectorEnabled = ConfigDefaults::zoneSelectorEnabled();
    m_zoneSelectorTriggerDistance = ConfigDefaults::triggerDistance();
    m_zoneSelectorPosition = static_cast<ZoneSelectorPosition>(ConfigDefaults::position());
    m_zoneSelectorLayoutMode = static_cast<ZoneSelectorLayoutMode>(ConfigDefaults::layoutMode());
    m_zoneSelectorPreviewWidth = ConfigDefaults::previewWidth();
    m_zoneSelectorPreviewHeight = ConfigDefaults::previewHeight();
    m_zoneSelectorPreviewLockAspect = ConfigDefaults::previewLockAspect();
    m_zoneSelectorGridColumns = ConfigDefaults::gridColumns();
    m_zoneSelectorSizeMode = static_cast<ZoneSelectorSizeMode>(ConfigDefaults::sizeMode());
    m_zoneSelectorMaxRows = ConfigDefaults::maxRows();

    // Shader Effects defaults (from .kcfg via ConfigDefaults)
    m_enableShaderEffects = ConfigDefaults::enableShaderEffects();
    m_shaderFrameRate = ConfigDefaults::shaderFrameRate();

    // Global Shortcuts defaults (from .kcfg via ConfigDefaults)
    m_openEditorShortcut = ConfigDefaults::openEditorShortcut();
    m_previousLayoutShortcut = QStringLiteral("Meta+Alt+[");
    m_nextLayoutShortcut = QStringLiteral("Meta+Alt+]");
    m_quickLayoutShortcuts[0] = ConfigDefaults::quickLayout1Shortcut();
    m_quickLayoutShortcuts[1] = ConfigDefaults::quickLayout2Shortcut();
    m_quickLayoutShortcuts[2] = ConfigDefaults::quickLayout3Shortcut();
    m_quickLayoutShortcuts[3] = ConfigDefaults::quickLayout4Shortcut();
    m_quickLayoutShortcuts[4] = ConfigDefaults::quickLayout5Shortcut();
    m_quickLayoutShortcuts[5] = ConfigDefaults::quickLayout6Shortcut();
    m_quickLayoutShortcuts[6] = ConfigDefaults::quickLayout7Shortcut();
    m_quickLayoutShortcuts[7] = ConfigDefaults::quickLayout8Shortcut();
    m_quickLayoutShortcuts[8] = ConfigDefaults::quickLayout9Shortcut();

    // Keyboard Navigation Shortcuts defaults (from .kcfg via ConfigDefaults)
    m_moveWindowLeftShortcut = ConfigDefaults::moveWindowLeftShortcut();
    m_moveWindowRightShortcut = ConfigDefaults::moveWindowRightShortcut();
    m_moveWindowUpShortcut = ConfigDefaults::moveWindowUpShortcut();
    m_moveWindowDownShortcut = ConfigDefaults::moveWindowDownShortcut();
    m_focusZoneLeftShortcut = ConfigDefaults::focusZoneLeftShortcut();
    m_focusZoneRightShortcut = ConfigDefaults::focusZoneRightShortcut();
    m_focusZoneUpShortcut = ConfigDefaults::focusZoneUpShortcut();
    m_focusZoneDownShortcut = ConfigDefaults::focusZoneDownShortcut();
    m_pushToEmptyZoneShortcut = ConfigDefaults::pushToEmptyZoneShortcut();
    m_restoreWindowSizeShortcut = ConfigDefaults::restoreWindowSizeShortcut();
    m_toggleWindowFloatShortcut = ConfigDefaults::toggleWindowFloatShortcut();

    // Swap Window Shortcuts (from .kcfg via ConfigDefaults)
    m_swapWindowLeftShortcut = ConfigDefaults::swapWindowLeftShortcut();
    m_swapWindowRightShortcut = ConfigDefaults::swapWindowRightShortcut();
    m_swapWindowUpShortcut = ConfigDefaults::swapWindowUpShortcut();
    m_swapWindowDownShortcut = ConfigDefaults::swapWindowDownShortcut();

    // Snap to Zone by Number Shortcuts (from .kcfg via ConfigDefaults)
    m_snapToZoneShortcuts[0] = ConfigDefaults::snapToZone1Shortcut();
    m_snapToZoneShortcuts[1] = ConfigDefaults::snapToZone2Shortcut();
    m_snapToZoneShortcuts[2] = ConfigDefaults::snapToZone3Shortcut();
    m_snapToZoneShortcuts[3] = ConfigDefaults::snapToZone4Shortcut();
    m_snapToZoneShortcuts[4] = ConfigDefaults::snapToZone5Shortcut();
    m_snapToZoneShortcuts[5] = ConfigDefaults::snapToZone6Shortcut();
    m_snapToZoneShortcuts[6] = ConfigDefaults::snapToZone7Shortcut();
    m_snapToZoneShortcuts[7] = ConfigDefaults::snapToZone8Shortcut();
    m_snapToZoneShortcuts[8] = ConfigDefaults::snapToZone9Shortcut();

    // Rotate Windows Shortcuts (from .kcfg via ConfigDefaults)
    m_rotateWindowsClockwiseShortcut = ConfigDefaults::rotateWindowsClockwiseShortcut();
    m_rotateWindowsCounterclockwiseShortcut = ConfigDefaults::rotateWindowsCounterclockwiseShortcut();

    // Cycle Windows in Zone Shortcuts (from .kcfg via ConfigDefaults)
    m_cycleWindowForwardShortcut = ConfigDefaults::cycleWindowForwardShortcut();
    m_cycleWindowBackwardShortcut = ConfigDefaults::cycleWindowBackwardShortcut();

    // ═══════════════════════════════════════════════════════════════════════════
    // Autotiling Settings (from .kcfg via ConfigDefaults)
    // ═══════════════════════════════════════════════════════════════════════════
    m_autotileEnabled = ConfigDefaults::autotileEnabled();
    m_autotileAlgorithm = ConfigDefaults::autotileAlgorithm();
    m_autotileSplitRatio = ConfigDefaults::autotileSplitRatio();
    m_autotileMasterCount = ConfigDefaults::autotileMasterCount();
    m_autotileInnerGap = ConfigDefaults::autotileInnerGap();
    m_autotileOuterGap = ConfigDefaults::autotileOuterGap();
    m_autotileFocusNewWindows = ConfigDefaults::autotileFocusNewWindows();
    m_autotileSmartGaps = ConfigDefaults::autotileSmartGaps();
    m_autotileInsertPosition = static_cast<AutotileInsertPosition>(ConfigDefaults::autotileInsertPosition());

    // Autotile Animation Settings
    m_autotileAnimationsEnabled = ConfigDefaults::autotileAnimationsEnabled();
    m_autotileAnimationDuration = ConfigDefaults::autotileAnimationDuration();

    // Additional Autotiling Settings
    m_autotileFocusFollowsMouse = ConfigDefaults::autotileFocusFollowsMouse();
    m_autotileRespectMinimumSize = ConfigDefaults::autotileRespectMinimumSize();
    m_autotileShowActiveBorder = ConfigDefaults::autotileShowActiveBorder();
    m_autotileActiveBorderWidth = ConfigDefaults::autotileActiveBorderWidth();
    m_autotileUseSystemBorderColor = ConfigDefaults::autotileUseSystemBorderColor();
    // Get system highlight color as default for custom border color
    KColorScheme resetBorderScheme(QPalette::Active, KColorScheme::Selection);
    m_autotileActiveBorderColor = resetBorderScheme.background(KColorScheme::ActiveBackground).color();
    m_autotileMonocleHideOthers = ConfigDefaults::autotileMonocleHideOthers();
    m_autotileMonocleShowTabs = ConfigDefaults::autotileMonocleShowTabs();

    // Autotiling Shortcuts (from .kcfg via ConfigDefaults, Bismuth-compatible)
    m_autotileToggleShortcut = ConfigDefaults::autotileToggleShortcut();
    m_autotileFocusMasterShortcut = ConfigDefaults::autotileFocusMasterShortcut();
    m_autotileSwapMasterShortcut = ConfigDefaults::autotileSwapMasterShortcut();
    m_autotileIncMasterRatioShortcut = ConfigDefaults::autotileIncMasterRatioShortcut();
    m_autotileDecMasterRatioShortcut = ConfigDefaults::autotileDecMasterRatioShortcut();
    m_autotileIncMasterCountShortcut = ConfigDefaults::autotileIncMasterCountShortcut();
    m_autotileDecMasterCountShortcut = ConfigDefaults::autotileDecMasterCountShortcut();
    m_autotileRetileShortcut = ConfigDefaults::autotileRetileShortcut();

    Q_EMIT settingsChanged();
}

void Settings::loadColorsFromFile(const QString& filePath)
{
    // Support pywal colors.json or simple color list
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }

    QTextStream stream(&file);
    QString content = stream.readAll();

    // Try to parse as pywal JSON
    if (filePath.endsWith(QStringLiteral(".json"))) {
        QJsonDocument doc = QJsonDocument::fromJson(content.toUtf8());
        if (!doc.isNull()) {
            QJsonObject colors = doc.object()[JsonKeys::Colors].toObject();
            if (!colors.isEmpty()) {
                // Use pywal colors with standard alpha values
                QColor accent(colors[QStringLiteral("color4")].toString());
                QColor bg(colors[QStringLiteral("color0")].toString());
                QColor fg(colors[QStringLiteral("color7")].toString());

                accent.setAlpha(Defaults::HighlightAlpha);
                setHighlightColor(accent);

                bg.setAlpha(Defaults::InactiveAlpha);
                setInactiveColor(bg);

                fg.setAlpha(Defaults::BorderAlpha);
                setBorderColor(fg);
                setNumberColor(fg);

                m_useSystemColors = false;
                Q_EMIT useSystemColorsChanged();
                return;
            }
        }
    }

    // Try to parse as simple color list (one hex per line)
    QStringList lines = content.split(QRegularExpression(QStringLiteral("[\r\n]+")));
    if (lines.size() >= 8) {
        QColor accent(lines[4].trimmed());
        QColor bg(lines[0].trimmed());
        QColor fg(lines[7].trimmed());

        if (accent.isValid() && bg.isValid() && fg.isValid()) {
            accent.setAlpha(Defaults::HighlightAlpha);
            setHighlightColor(accent);

            bg.setAlpha(Defaults::InactiveAlpha);
            setInactiveColor(bg);

            fg.setAlpha(Defaults::BorderAlpha);
            setBorderColor(fg);
            setNumberColor(fg);

            m_useSystemColors = false;
            Q_EMIT useSystemColorsChanged();
        }
    }
}

void Settings::applySystemColorScheme()
{
    KColorScheme scheme(QPalette::Active, KColorScheme::Selection);

    QColor highlight = scheme.background(KColorScheme::ActiveBackground).color();
    highlight.setAlpha(Defaults::HighlightAlpha);
    m_highlightColor = highlight;

    QColor inactive = scheme.background(KColorScheme::NormalBackground).color();
    inactive.setAlpha(Defaults::InactiveAlpha);
    m_inactiveColor = inactive;

    QColor border = scheme.foreground(KColorScheme::NormalText).color();
    border.setAlpha(Defaults::BorderAlpha);
    m_borderColor = border;

    m_numberColor = scheme.foreground(KColorScheme::NormalText).color();

    Q_EMIT highlightColorChanged();
    Q_EMIT inactiveColorChanged();
    Q_EMIT borderColorChanged();
    Q_EMIT numberColorChanged();
}

} // namespace PlasmaZones
