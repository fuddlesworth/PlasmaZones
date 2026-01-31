// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settings.h"
#include "colorimporter.h"
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
#include <climits> // For INT_MAX in readValidatedInt

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

// ═══════════════════════════════════════════════════════════════════════════════
// DRY Helper Methods - Consolidate repeated validation patterns in load()
// ═══════════════════════════════════════════════════════════════════════════════

int Settings::readValidatedInt(const KConfigGroup& group, const char* key, int defaultValue,
                               int min, int max, const char* settingName)
{
    int value = group.readEntry(QLatin1String(key), defaultValue);
    if (value < min || value > max) {
        qCWarning(lcConfig) << "Invalid" << settingName << ":" << value
                            << "using default (must be" << min << "-" << max << ")";
        value = defaultValue;
    }
    return value;
}

QColor Settings::readValidatedColor(const KConfigGroup& group, const char* key,
                                    const QColor& defaultValue, const char* settingName)
{
    QColor color = group.readEntry(QLatin1String(key), defaultValue);
    if (!color.isValid()) {
        qCWarning(lcConfig) << "Invalid" << settingName << "color, using default";
        color = defaultValue;
    }
    return color;
}

void Settings::loadIndexedShortcuts(const KConfigGroup& group, const QString& keyPattern,
                                    QString (&shortcuts)[9], const QString (&defaults)[9])
{
    for (int i = 0; i < 9; ++i) {
        QString key = keyPattern.arg(i + 1);
        shortcuts[i] = group.readEntry(key, defaults[i]);
    }
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
    m_shiftDragToActivate = activation.readEntry(QLatin1String("ShiftDrag"), ConfigDefaults::shiftDrag()); // Deprecated

    // New modifier settings with migration from old boolean
    int dragMod = activation.readEntry(QLatin1String("DragActivationModifier"), -1);
    if (dragMod == -1) {
        if (activation.hasKey(QLatin1String("ShiftDrag"))) {
            // Migrate from old shiftDragToActivate setting
            if (m_shiftDragToActivate) {
                m_dragActivationModifier = DragModifier::Shift;
            } else {
                m_dragActivationModifier = static_cast<DragModifier>(ConfigDefaults::dragActivationModifier());
                qCDebug(lcConfig)
                    << "Migrated ShiftDrag=false to DragActivationModifier=Alt (was causing login overlay issue)";
            }
            activation.writeEntry(QLatin1String("DragActivationModifier"), static_cast<int>(m_dragActivationModifier));
        } else {
            // New install: use default from .kcfg
            m_dragActivationModifier = static_cast<DragModifier>(ConfigDefaults::dragActivationModifier());
            activation.writeEntry(QLatin1String("DragActivationModifier"), static_cast<int>(m_dragActivationModifier));
        }
    } else {
        m_dragActivationModifier =
            static_cast<DragModifier>(qBound(0, dragMod, static_cast<int>(DragModifier::CtrlAltMeta)));
    }
    qCDebug(lcConfig) << "Loaded DragActivationModifier=" << static_cast<int>(m_dragActivationModifier);

    // Skip-snap modifier: hold this to move window without snapping
    int skipMod = activation.readEntry(QLatin1String("SkipSnapModifier"), ConfigDefaults::skipSnapModifier());
    m_skipSnapModifier = static_cast<DragModifier>(qBound(0, skipMod, 8));

    // Multi-zone modifier: hold this to span windows across multiple zones
    int multiZoneMod = activation.readEntry(QLatin1String("MultiZoneModifier"), ConfigDefaults::multiZoneModifier());
    if (multiZoneMod < 0 || multiZoneMod > static_cast<int>(DragModifier::CtrlAltMeta)) {
        qCWarning(lcConfig) << "Invalid MultiZoneModifier value:" << multiZoneMod << "using default";
        multiZoneMod = ConfigDefaults::multiZoneModifier();
    }
    m_multiZoneModifier = static_cast<DragModifier>(multiZoneMod);
    qCDebug(lcConfig) << "Loaded MultiZoneModifier=" << multiZoneMod;

    m_middleClickMultiZone = activation.readEntry(QLatin1String("MiddleClickMultiZone"), ConfigDefaults::middleClickMultiZone());

    // Display (defaults from .kcfg via ConfigDefaults)
    m_showZonesOnAllMonitors = display.readEntry(QLatin1String("ShowOnAllMonitors"), ConfigDefaults::showOnAllMonitors());
    m_disabledMonitors = display.readEntry(QLatin1String("DisabledMonitors"), QStringList());
    m_showZoneNumbers = display.readEntry(QLatin1String("ShowNumbers"), ConfigDefaults::showNumbers());
    m_flashZonesOnSwitch = display.readEntry(QLatin1String("FlashOnSwitch"), ConfigDefaults::flashOnSwitch());
    m_showOsdOnLayoutSwitch = display.readEntry(QLatin1String("ShowOsdOnLayoutSwitch"), ConfigDefaults::showOsdOnLayoutSwitch());
    m_showNavigationOsd = display.readEntry(QLatin1String("ShowNavigationOsd"), ConfigDefaults::showNavigationOsd());
    m_osdStyle = static_cast<OsdStyle>(readValidatedInt(display, "OsdStyle", ConfigDefaults::osdStyle(), 0, 2, "OSD style"));

    // Appearance with validation (defaults from .kcfg via ConfigDefaults)
    m_useSystemColors = appearance.readEntry(QLatin1String("UseSystemColors"), ConfigDefaults::useSystemColors());

    // Validate colors using DRY helper
    m_highlightColor = readValidatedColor(appearance, "HighlightColor", ConfigDefaults::highlightColor(), "highlight");
    m_inactiveColor = readValidatedColor(appearance, "InactiveColor", ConfigDefaults::inactiveColor(), "inactive");
    m_borderColor = readValidatedColor(appearance, "BorderColor", ConfigDefaults::borderColor(), "border");
    m_numberColor = readValidatedColor(appearance, "NumberColor", ConfigDefaults::numberColor(), "number");

    // Validate opacity (0.0 to 1.0)
    qreal activeOpacity = appearance.readEntry(QLatin1String("ActiveOpacity"), ConfigDefaults::activeOpacity());
    if (activeOpacity < 0.0 || activeOpacity > 1.0) {
        qCWarning(lcConfig) << "Invalid active opacity:" << activeOpacity << "clamping to valid range";
        activeOpacity = qBound(0.0, activeOpacity, 1.0);
    }
    m_activeOpacity = activeOpacity;

    qreal inactiveOpacity = appearance.readEntry(QLatin1String("InactiveOpacity"), ConfigDefaults::inactiveOpacity());
    if (inactiveOpacity < 0.0 || inactiveOpacity > 1.0) {
        qCWarning(lcConfig) << "Invalid inactive opacity:" << inactiveOpacity << "clamping to valid range";
        inactiveOpacity = qBound(0.0, inactiveOpacity, 1.0);
    }
    m_inactiveOpacity = inactiveOpacity;

    // Validate dimensions (non-negative) using DRY helper with INT_MAX as upper bound
    m_borderWidth = readValidatedInt(appearance, "BorderWidth", ConfigDefaults::borderWidth(), 0, INT_MAX, "border width");
    m_borderRadius = readValidatedInt(appearance, "BorderRadius", ConfigDefaults::borderRadius(), 0, INT_MAX, "border radius");

    m_enableBlur = appearance.readEntry(QLatin1String("EnableBlur"), ConfigDefaults::enableBlur());

    // Zones with validation (defaults from .kcfg via ConfigDefaults) using DRY helper
    m_zonePadding = readValidatedInt(zones, "Padding", ConfigDefaults::zonePadding(), 0, INT_MAX, "zone padding");
    m_outerGap = readValidatedInt(zones, "OuterGap", ConfigDefaults::outerGap(), 0, INT_MAX, "outer gap");
    m_adjacentThreshold = readValidatedInt(zones, "AdjacentThreshold", ConfigDefaults::adjacentThreshold(), 0, INT_MAX, "adjacent threshold");

    // Performance and behavior settings with validation using DRY helper
    m_pollIntervalMs = readValidatedInt(zones, "PollIntervalMs", ConfigDefaults::pollIntervalMs(), 10, 1000, "poll interval");
    m_minimumZoneSizePx = readValidatedInt(zones, "MinimumZoneSizePx", ConfigDefaults::minimumZoneSizePx(), 50, 500, "minimum zone size");
    m_minimumZoneDisplaySizePx = readValidatedInt(zones, "MinimumZoneDisplaySizePx", ConfigDefaults::minimumZoneDisplaySizePx(), 1, 50, "minimum zone display size");

    // Behavior (defaults from .kcfg via ConfigDefaults)
    m_keepWindowsInZonesOnResolutionChange = behavior.readEntry(QLatin1String("KeepOnResolutionChange"), ConfigDefaults::keepWindowsInZonesOnResolutionChange());
    m_moveNewWindowsToLastZone = behavior.readEntry(QLatin1String("MoveNewToLastZone"), ConfigDefaults::moveNewWindowsToLastZone());
    m_restoreOriginalSizeOnUnsnap = behavior.readEntry(QLatin1String("RestoreSizeOnUnsnap"), ConfigDefaults::restoreOriginalSizeOnUnsnap());
    int stickyHandling =
        behavior.readEntry(QLatin1String("StickyWindowHandling"), ConfigDefaults::stickyWindowHandling());
    m_stickyWindowHandling =
        static_cast<StickyWindowHandling>(qBound(static_cast<int>(StickyWindowHandling::TreatAsNormal), stickyHandling,
                                                 static_cast<int>(StickyWindowHandling::IgnoreAll)));
    m_restoreWindowsToZonesOnLogin = behavior.readEntry(QLatin1String("RestoreWindowsToZonesOnLogin"), ConfigDefaults::restoreWindowsToZonesOnLogin());
    // Normalize UUID to default format (with braces) for consistent comparison
    // Handles migration from configs saved with WithoutBraces format
    m_defaultLayoutId = normalizeUuidString(behavior.readEntry(QLatin1String("DefaultLayoutId"), QString()));

    // Exclusions (defaults from .kcfg via ConfigDefaults)
    m_excludedApplications = exclusions.readEntry(QLatin1String("Applications"), QStringList());
    m_excludedWindowClasses = exclusions.readEntry(QLatin1String("WindowClasses"), QStringList());
    m_excludeTransientWindows = exclusions.readEntry(QLatin1String("ExcludeTransientWindows"), ConfigDefaults::excludeTransientWindows());
    int minWidth = exclusions.readEntry(QLatin1String("MinimumWindowWidth"), ConfigDefaults::minimumWindowWidth());
    m_minimumWindowWidth = qBound(0, minWidth, 2000);  // Match kcfg max for 4K monitors
    int minHeight = exclusions.readEntry(QLatin1String("MinimumWindowHeight"), ConfigDefaults::minimumWindowHeight());
    m_minimumWindowHeight = qBound(0, minHeight, 2000);  // Match kcfg max for 4K monitors

    // Zone Selector (defaults from .kcfg via ConfigDefaults)
    KConfigGroup zoneSelector = config->group(QStringLiteral("ZoneSelector"));
    m_zoneSelectorEnabled = zoneSelector.readEntry(QLatin1String("Enabled"), ConfigDefaults::zoneSelectorEnabled());
    m_zoneSelectorTriggerDistance = readValidatedInt(zoneSelector, "TriggerDistance", ConfigDefaults::triggerDistance(), 10, 200, "zone selector trigger distance");
    int selectorPos = zoneSelector.readEntry(QLatin1String("Position"), ConfigDefaults::position());
    // Valid positions are 0-8 except 4 (center)
    if (selectorPos >= 0 && selectorPos <= 8 && selectorPos != 4) {
        m_zoneSelectorPosition = static_cast<ZoneSelectorPosition>(selectorPos);
    } else {
        m_zoneSelectorPosition = static_cast<ZoneSelectorPosition>(ConfigDefaults::position());
    }
    int selectorMode = zoneSelector.readEntry(QLatin1String("LayoutMode"), ConfigDefaults::layoutMode());
    m_zoneSelectorLayoutMode = static_cast<ZoneSelectorLayoutMode>(
        qBound(0, selectorMode, static_cast<int>(ZoneSelectorLayoutMode::Vertical)));
    m_zoneSelectorPreviewWidth = readValidatedInt(zoneSelector, "PreviewWidth", ConfigDefaults::previewWidth(), 80, 400, "zone selector preview width");
    m_zoneSelectorPreviewHeight = readValidatedInt(zoneSelector, "PreviewHeight", ConfigDefaults::previewHeight(), 60, 300, "zone selector preview height");
    m_zoneSelectorPreviewLockAspect = zoneSelector.readEntry(QLatin1String("PreviewLockAspect"), ConfigDefaults::previewLockAspect());
    m_zoneSelectorGridColumns = readValidatedInt(zoneSelector, "GridColumns", ConfigDefaults::gridColumns(), 1, 10, "zone selector grid columns");

    // Size mode (Auto/Manual)
    int sizeMode = zoneSelector.readEntry(QLatin1String("SizeMode"), ConfigDefaults::sizeMode());
    m_zoneSelectorSizeMode =
        static_cast<ZoneSelectorSizeMode>(qBound(0, sizeMode, static_cast<int>(ZoneSelectorSizeMode::Manual)));

    // Max visible rows before scrolling (Auto mode)
    m_zoneSelectorMaxRows = readValidatedInt(zoneSelector, "MaxRows", ConfigDefaults::maxRows(), 1, 10, "zone selector max rows");

    // Shader Effects (defaults from .kcfg via ConfigDefaults)
    KConfigGroup shaders = config->group(QStringLiteral("Shaders"));
    m_enableShaderEffects = shaders.readEntry(QLatin1String("EnableShaderEffects"), ConfigDefaults::enableShaderEffects());
    m_shaderFrameRate = qBound(30, shaders.readEntry(QLatin1String("ShaderFrameRate"), ConfigDefaults::shaderFrameRate()), 144);

    // Global Shortcuts (defaults from .kcfg via ConfigDefaults)
    KConfigGroup globalShortcuts = config->group(QStringLiteral("GlobalShortcuts"));
    m_openEditorShortcut = globalShortcuts.readEntry(QLatin1String("OpenEditorShortcut"), ConfigDefaults::openEditorShortcut());
    m_previousLayoutShortcut = globalShortcuts.readEntry(QLatin1String("PreviousLayoutShortcut"), QStringLiteral("Meta+Alt+["));
    m_nextLayoutShortcut = globalShortcuts.readEntry(QLatin1String("NextLayoutShortcut"), QStringLiteral("Meta+Alt+]"));
    // Quick layout shortcuts - use DRY helper with ConfigDefaults
    const QString quickLayoutDefaults[9] = {
        ConfigDefaults::quickLayout1Shortcut(), ConfigDefaults::quickLayout2Shortcut(),
        ConfigDefaults::quickLayout3Shortcut(), ConfigDefaults::quickLayout4Shortcut(),
        ConfigDefaults::quickLayout5Shortcut(), ConfigDefaults::quickLayout6Shortcut(),
        ConfigDefaults::quickLayout7Shortcut(), ConfigDefaults::quickLayout8Shortcut(),
        ConfigDefaults::quickLayout9Shortcut()
    };
    loadIndexedShortcuts(globalShortcuts, QStringLiteral("QuickLayout%1Shortcut"), m_quickLayoutShortcuts, quickLayoutDefaults);

    // Keyboard Navigation Shortcuts (defaults from .kcfg via ConfigDefaults)
    // Shortcut pattern philosophy for consistency and KDE conflict avoidance:
    //   Meta+Alt+{key}         = Layout operations ([, ], 1-9, Return, Escape, F)
    //   Meta+Alt+Shift+Arrow   = Window zone movement
    //   Alt+Shift+Arrow        = Focus zone navigation (lighter action, no Meta)
    //   Meta+Ctrl+{1-9}        = Direct zone snapping
    // Meta+Shift+Left/Right conflicts with KDE's "Window to Next/Previous Screen";
    // we use Meta+Alt+Shift+Arrow instead.
    KConfigGroup navigationShortcuts = config->group(QStringLiteral("NavigationShortcuts"));
    m_moveWindowLeftShortcut = navigationShortcuts.readEntry(QLatin1String("MoveWindowLeft"), ConfigDefaults::moveWindowLeftShortcut());
    m_moveWindowRightShortcut =
        navigationShortcuts.readEntry(QLatin1String("MoveWindowRight"), ConfigDefaults::moveWindowRightShortcut());
    m_moveWindowUpShortcut = navigationShortcuts.readEntry(QLatin1String("MoveWindowUp"), ConfigDefaults::moveWindowUpShortcut());
    m_moveWindowDownShortcut = navigationShortcuts.readEntry(QLatin1String("MoveWindowDown"), ConfigDefaults::moveWindowDownShortcut());
    // Meta+Arrow conflicts with KDE's Quick Tile; we use Alt+Shift+Arrow instead.
    m_focusZoneLeftShortcut = navigationShortcuts.readEntry(QLatin1String("FocusZoneLeft"), ConfigDefaults::focusZoneLeftShortcut());
    m_focusZoneRightShortcut = navigationShortcuts.readEntry(QLatin1String("FocusZoneRight"), ConfigDefaults::focusZoneRightShortcut());
    m_focusZoneUpShortcut = navigationShortcuts.readEntry(QLatin1String("FocusZoneUp"), ConfigDefaults::focusZoneUpShortcut());
    m_focusZoneDownShortcut = navigationShortcuts.readEntry(QLatin1String("FocusZoneDown"), ConfigDefaults::focusZoneDownShortcut());
    m_pushToEmptyZoneShortcut = navigationShortcuts.readEntry(QLatin1String("PushToEmptyZone"), ConfigDefaults::pushToEmptyZoneShortcut());
    m_restoreWindowSizeShortcut = navigationShortcuts.readEntry(QLatin1String("RestoreWindowSize"), ConfigDefaults::restoreWindowSizeShortcut());
    m_toggleWindowFloatShortcut = navigationShortcuts.readEntry(QLatin1String("ToggleWindowFloat"), ConfigDefaults::toggleWindowFloatShortcut());

    // Swap Window Shortcuts (Meta+Ctrl+Alt+Arrow)
    // Meta+Ctrl+Arrow conflicts with KDE's virtual desktop switching;
    // we add Alt to make Meta+Ctrl+Alt+Arrow for swap operations.
    m_swapWindowLeftShortcut =
        navigationShortcuts.readEntry(QLatin1String("SwapWindowLeft"), ConfigDefaults::swapWindowLeftShortcut());
    m_swapWindowRightShortcut =
        navigationShortcuts.readEntry(QLatin1String("SwapWindowRight"), ConfigDefaults::swapWindowRightShortcut());
    m_swapWindowUpShortcut = navigationShortcuts.readEntry(QLatin1String("SwapWindowUp"), ConfigDefaults::swapWindowUpShortcut());
    m_swapWindowDownShortcut = navigationShortcuts.readEntry(QLatin1String("SwapWindowDown"), ConfigDefaults::swapWindowDownShortcut());

    // Snap to Zone by Number Shortcuts (Meta+Ctrl+1-9) - using DRY helper
    const QString snapToZoneDefaults[9] = {
        ConfigDefaults::snapToZone1Shortcut(), ConfigDefaults::snapToZone2Shortcut(),
        ConfigDefaults::snapToZone3Shortcut(), ConfigDefaults::snapToZone4Shortcut(),
        ConfigDefaults::snapToZone5Shortcut(), ConfigDefaults::snapToZone6Shortcut(),
        ConfigDefaults::snapToZone7Shortcut(), ConfigDefaults::snapToZone8Shortcut(),
        ConfigDefaults::snapToZone9Shortcut()
    };
    loadIndexedShortcuts(navigationShortcuts, QStringLiteral("SnapToZone%1"), m_snapToZoneShortcuts, snapToZoneDefaults);

    // Rotate Windows Shortcuts (Meta+Ctrl+[ / Meta+Ctrl+])
    // Rotates all windows in the current layout clockwise or counterclockwise
    m_rotateWindowsClockwiseShortcut =
        navigationShortcuts.readEntry(QLatin1String("RotateWindowsClockwise"), ConfigDefaults::rotateWindowsClockwiseShortcut());
    m_rotateWindowsCounterclockwiseShortcut =
        navigationShortcuts.readEntry(QLatin1String("RotateWindowsCounterclockwise"), ConfigDefaults::rotateWindowsCounterclockwiseShortcut());

    // Cycle Windows in Zone Shortcuts (Meta+Alt+. / Meta+Alt+,)
    // Cycles focus between windows stacked in the same zone (monocle-style navigation)
    m_cycleWindowForwardShortcut =
        navigationShortcuts.readEntry(QLatin1String("CycleWindowForward"), ConfigDefaults::cycleWindowForwardShortcut());
    m_cycleWindowBackwardShortcut =
        navigationShortcuts.readEntry(QLatin1String("CycleWindowBackward"), ConfigDefaults::cycleWindowBackwardShortcut());

    // ═══════════════════════════════════════════════════════════════════════════
    // Autotiling Settings (defaults from .kcfg via ConfigDefaults)
    // Note: AutotileDefaults in constants.h still used for min/max bounds
    // ═══════════════════════════════════════════════════════════════════════════
    KConfigGroup autotiling = config->group(QStringLiteral("Autotiling"));

    m_autotileEnabled = autotiling.readEntry(QLatin1String("AutotileEnabled"), ConfigDefaults::autotileEnabled());
    m_autotileAlgorithm = autotiling.readEntry(QLatin1String("AutotileAlgorithm"), ConfigDefaults::autotileAlgorithm());

    qreal splitRatio = autotiling.readEntry(QLatin1String("AutotileSplitRatio"), ConfigDefaults::autotileSplitRatio());
    if (splitRatio < AutotileDefaults::MinSplitRatio || splitRatio > AutotileDefaults::MaxSplitRatio) {
        qCWarning(lcConfig) << "Invalid autotile split ratio:" << splitRatio << "clamping to valid range";
        splitRatio = qBound(AutotileDefaults::MinSplitRatio, splitRatio, AutotileDefaults::MaxSplitRatio);
    }
    m_autotileSplitRatio = splitRatio;

    int masterCount = autotiling.readEntry(QLatin1String("AutotileMasterCount"), ConfigDefaults::autotileMasterCount());
    if (masterCount < AutotileDefaults::MinMasterCount || masterCount > AutotileDefaults::MaxMasterCount) {
        qCWarning(lcConfig) << "Invalid autotile master count:" << masterCount << "clamping to valid range";
        masterCount = qBound(AutotileDefaults::MinMasterCount, masterCount, AutotileDefaults::MaxMasterCount);
    }
    m_autotileMasterCount = masterCount;

    m_autotileInnerGap = readValidatedInt(autotiling, "AutotileInnerGap", ConfigDefaults::autotileInnerGap(),
                                          AutotileDefaults::MinGap, AutotileDefaults::MaxGap, "autotile inner gap");
    m_autotileOuterGap = readValidatedInt(autotiling, "AutotileOuterGap", ConfigDefaults::autotileOuterGap(),
                                          AutotileDefaults::MinGap, AutotileDefaults::MaxGap, "autotile outer gap");
    m_autotileFocusNewWindows = autotiling.readEntry(QLatin1String("AutotileFocusNewWindows"), ConfigDefaults::autotileFocusNewWindows());
    m_autotileSmartGaps = autotiling.readEntry(QLatin1String("AutotileSmartGaps"), ConfigDefaults::autotileSmartGaps());
    m_autotileInsertPosition = static_cast<AutotileInsertPosition>(
        readValidatedInt(autotiling, "AutotileInsertPosition", ConfigDefaults::autotileInsertPosition(), 0, 2, "autotile insert position"));

    // Autotile Animation Settings
    m_autotileAnimationsEnabled = autotiling.readEntry(QLatin1String("AutotileAnimationsEnabled"), ConfigDefaults::autotileAnimationsEnabled());
    m_autotileAnimationDuration = readValidatedInt(autotiling, "AutotileAnimationDuration", ConfigDefaults::autotileAnimationDuration(), 50, 500, "autotile animation duration");

    // Additional Autotiling Settings
    m_autotileFocusFollowsMouse = autotiling.readEntry(QLatin1String("AutotileFocusFollowsMouse"), ConfigDefaults::autotileFocusFollowsMouse());
    m_autotileRespectMinimumSize = autotiling.readEntry(QLatin1String("AutotileRespectMinimumSize"), ConfigDefaults::autotileRespectMinimumSize());
    m_autotileShowActiveBorder = autotiling.readEntry(QLatin1String("AutotileShowActiveBorder"), ConfigDefaults::autotileShowActiveBorder());
    m_autotileActiveBorderWidth = readValidatedInt(autotiling, "AutotileActiveBorderWidth", ConfigDefaults::autotileActiveBorderWidth(), 1, 10, "autotile active border width");
    m_autotileUseSystemBorderColor = autotiling.readEntry(QLatin1String("AutotileUseSystemBorderColor"), ConfigDefaults::autotileUseSystemBorderColor());

    // Get system highlight color as default for custom border color
    KColorScheme borderScheme(QPalette::Active, KColorScheme::Selection);
    QColor systemHighlight = borderScheme.background(KColorScheme::ActiveBackground).color();
    m_autotileActiveBorderColor = readValidatedColor(autotiling, "AutotileActiveBorderColor", systemHighlight, "autotile active border");

    m_autotileMonocleHideOthers = autotiling.readEntry(QLatin1String("AutotileMonocleHideOthers"), ConfigDefaults::autotileMonocleHideOthers());
    m_autotileMonocleShowTabs = autotiling.readEntry(QLatin1String("AutotileMonocleShowTabs"), ConfigDefaults::autotileMonocleShowTabs());

    // Autotiling Shortcuts (defaults from .kcfg via ConfigDefaults, Bismuth-compatible)
    KConfigGroup autotileShortcuts = config->group(QStringLiteral("AutotileShortcuts"));
    m_autotileToggleShortcut = autotileShortcuts.readEntry(QLatin1String("ToggleShortcut"), ConfigDefaults::autotileToggleShortcut());
    m_autotileFocusMasterShortcut = autotileShortcuts.readEntry(QLatin1String("FocusMasterShortcut"), ConfigDefaults::autotileFocusMasterShortcut());
    m_autotileSwapMasterShortcut = autotileShortcuts.readEntry(QLatin1String("SwapMasterShortcut"), ConfigDefaults::autotileSwapMasterShortcut());
    m_autotileIncMasterRatioShortcut = autotileShortcuts.readEntry(QLatin1String("IncMasterRatioShortcut"), ConfigDefaults::autotileIncMasterRatioShortcut());
    m_autotileDecMasterRatioShortcut = autotileShortcuts.readEntry(QLatin1String("DecMasterRatioShortcut"), ConfigDefaults::autotileDecMasterRatioShortcut());
    m_autotileIncMasterCountShortcut = autotileShortcuts.readEntry(QLatin1String("IncMasterCountShortcut"), ConfigDefaults::autotileIncMasterCountShortcut());
    m_autotileDecMasterCountShortcut = autotileShortcuts.readEntry(QLatin1String("DecMasterCountShortcut"), ConfigDefaults::autotileDecMasterCountShortcut());
    m_autotileRetileShortcut = autotileShortcuts.readEntry(QLatin1String("RetileShortcut"), ConfigDefaults::autotileRetileShortcut());

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
    activation.writeEntry(QLatin1String("ShiftDrag"), m_shiftDragToActivate); // Deprecated, kept for compatibility
    activation.writeEntry(QLatin1String("DragActivationModifier"), static_cast<int>(m_dragActivationModifier));
    activation.writeEntry(QLatin1String("SkipSnapModifier"), static_cast<int>(m_skipSnapModifier));
    activation.writeEntry(QLatin1String("MultiZoneModifier"), static_cast<int>(m_multiZoneModifier));
    activation.writeEntry(QLatin1String("MiddleClickMultiZone"), m_middleClickMultiZone);

    // Display
    display.writeEntry(QLatin1String("ShowOnAllMonitors"), m_showZonesOnAllMonitors);
    display.writeEntry(QLatin1String("DisabledMonitors"), m_disabledMonitors);
    display.writeEntry(QLatin1String("ShowNumbers"), m_showZoneNumbers);
    display.writeEntry(QLatin1String("FlashOnSwitch"), m_flashZonesOnSwitch);
    display.writeEntry(QLatin1String("ShowOsdOnLayoutSwitch"), m_showOsdOnLayoutSwitch);
    display.writeEntry(QLatin1String("ShowNavigationOsd"), m_showNavigationOsd);
    display.writeEntry(QLatin1String("OsdStyle"), static_cast<int>(m_osdStyle));

    // Appearance
    appearance.writeEntry(QLatin1String("UseSystemColors"), m_useSystemColors);
    appearance.writeEntry(QLatin1String("HighlightColor"), m_highlightColor);
    appearance.writeEntry(QLatin1String("InactiveColor"), m_inactiveColor);
    appearance.writeEntry(QLatin1String("BorderColor"), m_borderColor);
    appearance.writeEntry(QLatin1String("NumberColor"), m_numberColor);
    appearance.writeEntry(QLatin1String("ActiveOpacity"), m_activeOpacity);
    appearance.writeEntry(QLatin1String("InactiveOpacity"), m_inactiveOpacity);
    appearance.writeEntry(QLatin1String("BorderWidth"), m_borderWidth);
    appearance.writeEntry(QLatin1String("BorderRadius"), m_borderRadius);
    appearance.writeEntry(QLatin1String("EnableBlur"), m_enableBlur);

    // Zones
    zones.writeEntry(QLatin1String("Padding"), m_zonePadding);
    zones.writeEntry(QLatin1String("OuterGap"), m_outerGap);
    zones.writeEntry(QLatin1String("AdjacentThreshold"), m_adjacentThreshold);

    // Performance and behavior
    zones.writeEntry(QLatin1String("PollIntervalMs"), m_pollIntervalMs);
    zones.writeEntry(QLatin1String("MinimumZoneSizePx"), m_minimumZoneSizePx);
    zones.writeEntry(QLatin1String("MinimumZoneDisplaySizePx"), m_minimumZoneDisplaySizePx);

    // Behavior
    behavior.writeEntry(QLatin1String("KeepOnResolutionChange"), m_keepWindowsInZonesOnResolutionChange);
    behavior.writeEntry(QLatin1String("MoveNewToLastZone"), m_moveNewWindowsToLastZone);
    behavior.writeEntry(QLatin1String("RestoreSizeOnUnsnap"), m_restoreOriginalSizeOnUnsnap);
    behavior.writeEntry(QLatin1String("StickyWindowHandling"), static_cast<int>(m_stickyWindowHandling));
    behavior.writeEntry(QLatin1String("RestoreWindowsToZonesOnLogin"), m_restoreWindowsToZonesOnLogin);
    behavior.writeEntry(QLatin1String("DefaultLayoutId"), m_defaultLayoutId);

    // Exclusions
    exclusions.writeEntry(QLatin1String("Applications"), m_excludedApplications);
    exclusions.writeEntry(QLatin1String("WindowClasses"), m_excludedWindowClasses);
    exclusions.writeEntry(QLatin1String("ExcludeTransientWindows"), m_excludeTransientWindows);
    exclusions.writeEntry(QLatin1String("MinimumWindowWidth"), m_minimumWindowWidth);
    exclusions.writeEntry(QLatin1String("MinimumWindowHeight"), m_minimumWindowHeight);

    // Zone Selector
    KConfigGroup zoneSelector = config->group(QStringLiteral("ZoneSelector"));
    zoneSelector.writeEntry(QLatin1String("Enabled"), m_zoneSelectorEnabled);
    zoneSelector.writeEntry(QLatin1String("TriggerDistance"), m_zoneSelectorTriggerDistance);
    zoneSelector.writeEntry(QLatin1String("Position"), static_cast<int>(m_zoneSelectorPosition));
    zoneSelector.writeEntry(QLatin1String("LayoutMode"), static_cast<int>(m_zoneSelectorLayoutMode));
    zoneSelector.writeEntry(QLatin1String("PreviewWidth"), m_zoneSelectorPreviewWidth);
    zoneSelector.writeEntry(QLatin1String("PreviewHeight"), m_zoneSelectorPreviewHeight);
    zoneSelector.writeEntry(QLatin1String("PreviewLockAspect"), m_zoneSelectorPreviewLockAspect);
    zoneSelector.writeEntry(QLatin1String("GridColumns"), m_zoneSelectorGridColumns);
    zoneSelector.writeEntry(QLatin1String("SizeMode"), static_cast<int>(m_zoneSelectorSizeMode));
    zoneSelector.writeEntry(QLatin1String("MaxRows"), m_zoneSelectorMaxRows);

    // Shader Effects
    KConfigGroup shaders = config->group(QStringLiteral("Shaders"));
    shaders.writeEntry(QLatin1String("EnableShaderEffects"), m_enableShaderEffects);
    shaders.writeEntry(QLatin1String("ShaderFrameRate"), m_shaderFrameRate);

    // Global Shortcuts
    KConfigGroup globalShortcuts = config->group(QStringLiteral("GlobalShortcuts"));
    globalShortcuts.writeEntry(QLatin1String("OpenEditorShortcut"), m_openEditorShortcut);
    globalShortcuts.writeEntry(QLatin1String("PreviousLayoutShortcut"), m_previousLayoutShortcut);
    globalShortcuts.writeEntry(QLatin1String("NextLayoutShortcut"), m_nextLayoutShortcut);
    for (int i = 0; i < 9; ++i) {
        QString key = QStringLiteral("QuickLayout%1Shortcut").arg(i + 1);
        globalShortcuts.writeEntry(key, m_quickLayoutShortcuts[i]);
    }

    // Keyboard Navigation Shortcuts (Phase 1 features)
    KConfigGroup navigationShortcuts = config->group(QStringLiteral("NavigationShortcuts"));
    navigationShortcuts.writeEntry(QLatin1String("MoveWindowLeft"), m_moveWindowLeftShortcut);
    navigationShortcuts.writeEntry(QLatin1String("MoveWindowRight"), m_moveWindowRightShortcut);
    navigationShortcuts.writeEntry(QLatin1String("MoveWindowUp"), m_moveWindowUpShortcut);
    navigationShortcuts.writeEntry(QLatin1String("MoveWindowDown"), m_moveWindowDownShortcut);
    navigationShortcuts.writeEntry(QLatin1String("FocusZoneLeft"), m_focusZoneLeftShortcut);
    navigationShortcuts.writeEntry(QLatin1String("FocusZoneRight"), m_focusZoneRightShortcut);
    navigationShortcuts.writeEntry(QLatin1String("FocusZoneUp"), m_focusZoneUpShortcut);
    navigationShortcuts.writeEntry(QLatin1String("FocusZoneDown"), m_focusZoneDownShortcut);
    navigationShortcuts.writeEntry(QLatin1String("PushToEmptyZone"), m_pushToEmptyZoneShortcut);
    navigationShortcuts.writeEntry(QLatin1String("RestoreWindowSize"), m_restoreWindowSizeShortcut);
    navigationShortcuts.writeEntry(QLatin1String("ToggleWindowFloat"), m_toggleWindowFloatShortcut);

    // Swap Window Shortcuts
    navigationShortcuts.writeEntry(QLatin1String("SwapWindowLeft"), m_swapWindowLeftShortcut);
    navigationShortcuts.writeEntry(QLatin1String("SwapWindowRight"), m_swapWindowRightShortcut);
    navigationShortcuts.writeEntry(QLatin1String("SwapWindowUp"), m_swapWindowUpShortcut);
    navigationShortcuts.writeEntry(QLatin1String("SwapWindowDown"), m_swapWindowDownShortcut);

    // Snap to Zone by Number Shortcuts
    for (int i = 0; i < 9; ++i) {
        QString key = QStringLiteral("SnapToZone%1").arg(i + 1);
        navigationShortcuts.writeEntry(key, m_snapToZoneShortcuts[i]);
    }

    // Rotate Windows Shortcuts
    navigationShortcuts.writeEntry(QLatin1String("RotateWindowsClockwise"), m_rotateWindowsClockwiseShortcut);
    navigationShortcuts.writeEntry(QLatin1String("RotateWindowsCounterclockwise"), m_rotateWindowsCounterclockwiseShortcut);

    // Cycle Windows in Zone Shortcuts
    navigationShortcuts.writeEntry(QLatin1String("CycleWindowForward"), m_cycleWindowForwardShortcut);
    navigationShortcuts.writeEntry(QLatin1String("CycleWindowBackward"), m_cycleWindowBackwardShortcut);

    // ═══════════════════════════════════════════════════════════════════════════
    // Autotiling Settings
    // ═══════════════════════════════════════════════════════════════════════════
    KConfigGroup autotiling = config->group(QStringLiteral("Autotiling"));
    autotiling.writeEntry(QLatin1String("AutotileEnabled"), m_autotileEnabled);
    autotiling.writeEntry(QLatin1String("AutotileAlgorithm"), m_autotileAlgorithm);
    autotiling.writeEntry(QLatin1String("AutotileSplitRatio"), m_autotileSplitRatio);
    autotiling.writeEntry(QLatin1String("AutotileMasterCount"), m_autotileMasterCount);
    autotiling.writeEntry(QLatin1String("AutotileInnerGap"), m_autotileInnerGap);
    autotiling.writeEntry(QLatin1String("AutotileOuterGap"), m_autotileOuterGap);
    autotiling.writeEntry(QLatin1String("AutotileFocusNewWindows"), m_autotileFocusNewWindows);
    autotiling.writeEntry(QLatin1String("AutotileSmartGaps"), m_autotileSmartGaps);
    autotiling.writeEntry(QLatin1String("AutotileInsertPosition"), static_cast<int>(m_autotileInsertPosition));

    // Autotile Animation Settings
    autotiling.writeEntry(QLatin1String("AutotileAnimationsEnabled"), m_autotileAnimationsEnabled);
    autotiling.writeEntry(QLatin1String("AutotileAnimationDuration"), m_autotileAnimationDuration);

    // Additional Autotiling Settings
    autotiling.writeEntry(QLatin1String("AutotileFocusFollowsMouse"), m_autotileFocusFollowsMouse);
    autotiling.writeEntry(QLatin1String("AutotileRespectMinimumSize"), m_autotileRespectMinimumSize);
    autotiling.writeEntry(QLatin1String("AutotileShowActiveBorder"), m_autotileShowActiveBorder);
    autotiling.writeEntry(QLatin1String("AutotileActiveBorderWidth"), m_autotileActiveBorderWidth);
    autotiling.writeEntry(QLatin1String("AutotileUseSystemBorderColor"), m_autotileUseSystemBorderColor);
    autotiling.writeEntry(QLatin1String("AutotileActiveBorderColor"), m_autotileActiveBorderColor);
    autotiling.writeEntry(QLatin1String("AutotileMonocleHideOthers"), m_autotileMonocleHideOthers);
    autotiling.writeEntry(QLatin1String("AutotileMonocleShowTabs"), m_autotileMonocleShowTabs);

    // Autotiling Shortcuts
    KConfigGroup autotileShortcuts = config->group(QStringLiteral("AutotileShortcuts"));
    autotileShortcuts.writeEntry(QLatin1String("ToggleShortcut"), m_autotileToggleShortcut);
    autotileShortcuts.writeEntry(QLatin1String("FocusMasterShortcut"), m_autotileFocusMasterShortcut);
    autotileShortcuts.writeEntry(QLatin1String("SwapMasterShortcut"), m_autotileSwapMasterShortcut);
    autotileShortcuts.writeEntry(QLatin1String("IncMasterRatioShortcut"), m_autotileIncMasterRatioShortcut);
    autotileShortcuts.writeEntry(QLatin1String("DecMasterRatioShortcut"), m_autotileDecMasterRatioShortcut);
    autotileShortcuts.writeEntry(QLatin1String("IncMasterCountShortcut"), m_autotileIncMasterCountShortcut);
    autotileShortcuts.writeEntry(QLatin1String("DecMasterCountShortcut"), m_autotileDecMasterCountShortcut);
    autotileShortcuts.writeEntry(QLatin1String("RetileShortcut"), m_autotileRetileShortcut);

    config->sync();
}

void Settings::reset()
{
    // DRY: Clear all config groups and reload with defaults
    // This avoids duplicating ~160 lines of default assignments
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));

    // Delete all setting groups (load() will use ConfigDefaults for missing keys)
    const QStringList groups = {
        QStringLiteral("Activation"),
        QStringLiteral("Display"),
        QStringLiteral("Appearance"),
        QStringLiteral("Zones"),
        QStringLiteral("Behavior"),
        QStringLiteral("Exclusions"),
        QStringLiteral("ZoneSelector"),
        QStringLiteral("Shaders"),
        QStringLiteral("GlobalShortcuts"),
        QStringLiteral("NavigationShortcuts"),
        QStringLiteral("Autotiling"),
        QStringLiteral("AutotileShortcuts")
    };

    for (const QString& groupName : groups) {
        config->deleteGroup(groupName);
    }
    config->sync();

    // Reload from (now empty) config - will use ConfigDefaults for all values
    load();

    qCDebug(lcConfig) << "Settings reset to defaults";
}

void Settings::loadColorsFromFile(const QString& filePath)
{
    // Delegate to ColorImporter (SRP: color parsing is separate concern)
    ColorImportResult result = ColorImporter::importFromFile(filePath);
    if (!result.success) {
        return;
    }

    // Apply imported colors
    setHighlightColor(result.highlightColor);
    setInactiveColor(result.inactiveColor);
    setBorderColor(result.borderColor);
    setNumberColor(result.numberColor);

    m_useSystemColors = false;
    Q_EMIT useSystemColorsChanged();
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
