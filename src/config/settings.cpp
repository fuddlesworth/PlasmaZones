// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settings.h"
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

namespace PlasmaZones {

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

void Settings::setDefaultLayoutId(const QString& layoutId)
{
    if (m_defaultLayoutId != layoutId) {
        m_defaultLayoutId = layoutId;
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

    // Activation with validation
    m_shiftDragToActivate = activation.readEntry("ShiftDrag", true); // Deprecated

    // New modifier settings with migration from old boolean
    int dragMod = activation.readEntry("DragActivationModifier", -1);
    if (dragMod == -1) {
        if (activation.hasKey("ShiftDrag")) {
            // Migrate from old shiftDragToActivate setting
            if (m_shiftDragToActivate) {
                m_dragActivationModifier = DragModifier::Shift;
            } else {
                m_dragActivationModifier = DragModifier::Alt;
                qCDebug(lcConfig)
                    << "Migrated ShiftDrag=false to DragActivationModifier=Alt (was causing login overlay issue)";
            }
            activation.writeEntry("DragActivationModifier", static_cast<int>(m_dragActivationModifier));
        } else {
            // New install: default Alt for zone activation
            m_dragActivationModifier = DragModifier::Alt;
            activation.writeEntry("DragActivationModifier", static_cast<int>(m_dragActivationModifier));
        }
    } else {
        m_dragActivationModifier =
            static_cast<DragModifier>(qBound(0, dragMod, static_cast<int>(DragModifier::CtrlAltMeta)));
    }
    qCDebug(lcConfig) << "Loaded DragActivationModifier=" << static_cast<int>(m_dragActivationModifier);

    // Skip-snap modifier: hold this to move window without snapping (default: Shift)
    int skipMod = activation.readEntry("SkipSnapModifier", static_cast<int>(DragModifier::Shift));
    m_skipSnapModifier = static_cast<DragModifier>(qBound(0, skipMod, 8));

    // Multi-zone modifier: hold this to span windows across multiple zones (default: Ctrl+Alt)
    int multiZoneMod = activation.readEntry("MultiZoneModifier", static_cast<int>(DragModifier::CtrlAlt));
    if (multiZoneMod < 0 || multiZoneMod > static_cast<int>(DragModifier::CtrlAltMeta)) {
        qCWarning(lcConfig) << "Invalid MultiZoneModifier value:" << multiZoneMod << "using default (CtrlAlt=5)";
        multiZoneMod = static_cast<int>(DragModifier::CtrlAlt);
    }
    m_multiZoneModifier = static_cast<DragModifier>(multiZoneMod);
    qCDebug(lcConfig) << "Loaded MultiZoneModifier=" << multiZoneMod;

    m_middleClickMultiZone = activation.readEntry("MiddleClickMultiZone", true);

    // Display
    m_showZonesOnAllMonitors = display.readEntry("ShowOnAllMonitors", false);
    m_disabledMonitors = display.readEntry("DisabledMonitors", QStringList());
    m_showZoneNumbers = display.readEntry("ShowNumbers", true);
    m_flashZonesOnSwitch = display.readEntry("FlashOnSwitch", true);
    m_showOsdOnLayoutSwitch = display.readEntry("ShowOsdOnLayoutSwitch", true);
    int osdStyleInt = display.readEntry("OsdStyle", static_cast<int>(OsdStyle::Preview));
    if (osdStyleInt < 0 || osdStyleInt > 2) {
        osdStyleInt = static_cast<int>(OsdStyle::Preview);
    }
    m_osdStyle = static_cast<OsdStyle>(osdStyleInt);

    // Appearance with validation
    m_useSystemColors = appearance.readEntry("UseSystemColors", true);

    // Validate colors
    QColor highlightColor = appearance.readEntry("HighlightColor", Defaults::HighlightColor);
    if (!highlightColor.isValid()) {
        qCWarning(lcConfig) << "Invalid highlight color, using default";
        highlightColor = Defaults::HighlightColor;
    }
    m_highlightColor = highlightColor;

    QColor inactiveColor = appearance.readEntry("InactiveColor", Defaults::InactiveColor);
    if (!inactiveColor.isValid()) {
        qCWarning(lcConfig) << "Invalid inactive color, using default";
        inactiveColor = Defaults::InactiveColor;
    }
    m_inactiveColor = inactiveColor;

    QColor borderColor = appearance.readEntry("BorderColor", Defaults::BorderColor);
    if (!borderColor.isValid()) {
        qCWarning(lcConfig) << "Invalid border color, using default";
        borderColor = Defaults::BorderColor;
    }
    m_borderColor = borderColor;

    QColor numberColor = appearance.readEntry("NumberColor", Defaults::NumberColor);
    if (!numberColor.isValid()) {
        qCWarning(lcConfig) << "Invalid number color, using default";
        numberColor = Defaults::NumberColor;
    }
    m_numberColor = numberColor;

    // Validate opacity (0.0 to 1.0)
    qreal activeOpacity = appearance.readEntry("ActiveOpacity", Defaults::Opacity);
    if (activeOpacity < 0.0 || activeOpacity > 1.0) {
        qCWarning(lcConfig) << "Invalid active opacity:" << activeOpacity << "clamping to valid range";
        activeOpacity = qBound(0.0, activeOpacity, 1.0);
    }
    m_activeOpacity = activeOpacity;

    qreal inactiveOpacity = appearance.readEntry("InactiveOpacity", Defaults::InactiveOpacity);
    if (inactiveOpacity < 0.0 || inactiveOpacity > 1.0) {
        qCWarning(lcConfig) << "Invalid inactive opacity:" << inactiveOpacity << "clamping to valid range";
        inactiveOpacity = qBound(0.0, inactiveOpacity, 1.0);
    }
    m_inactiveOpacity = inactiveOpacity;

    // Validate dimensions (non-negative)
    int borderWidth = appearance.readEntry("BorderWidth", Defaults::BorderWidth);
    if (borderWidth < 0) {
        qCWarning(lcConfig) << "Invalid border width:" << borderWidth << "using default";
        borderWidth = Defaults::BorderWidth;
    }
    m_borderWidth = borderWidth;

    int borderRadius = appearance.readEntry("BorderRadius", Defaults::BorderRadius);
    if (borderRadius < 0) {
        qCWarning(lcConfig) << "Invalid border radius:" << borderRadius << "using default";
        borderRadius = Defaults::BorderRadius;
    }
    m_borderRadius = borderRadius;

    m_enableBlur = appearance.readEntry("EnableBlur", true);

    // Zones with validation
    int zonePadding = zones.readEntry("Padding", Defaults::ZonePadding);
    if (zonePadding < 0) {
        qCWarning(lcConfig) << "Invalid zone padding:" << zonePadding << "using default";
        zonePadding = Defaults::ZonePadding;
    }
    m_zonePadding = zonePadding;

    int outerGap = zones.readEntry("OuterGap", Defaults::OuterGap);
    if (outerGap < 0) {
        qCWarning(lcConfig) << "Invalid outer gap:" << outerGap << "using default";
        outerGap = Defaults::OuterGap;
    }
    m_outerGap = outerGap;

    int adjacentThreshold = zones.readEntry("AdjacentThreshold", Defaults::AdjacentThreshold);
    if (adjacentThreshold < 0) {
        qCWarning(lcConfig) << "Invalid adjacent threshold:" << adjacentThreshold << "using default";
        adjacentThreshold = Defaults::AdjacentThreshold;
    }
    m_adjacentThreshold = adjacentThreshold;

    // Performance and behavior settings with validation (configurable constants)
    int pollIntervalMs = zones.readEntry("PollIntervalMs", Defaults::PollIntervalMs);
    if (pollIntervalMs < 10 || pollIntervalMs > 1000) {
        qCWarning(lcConfig) << "Invalid poll interval:" << pollIntervalMs << "using default (must be 10-1000ms)";
        pollIntervalMs = Defaults::PollIntervalMs;
    }
    m_pollIntervalMs = pollIntervalMs;

    int minimumZoneSizePx = zones.readEntry("MinimumZoneSizePx", Defaults::MinimumZoneSizePx);
    if (minimumZoneSizePx < 50 || minimumZoneSizePx > 500) {
        qCWarning(lcConfig) << "Invalid minimum zone size:" << minimumZoneSizePx << "using default (must be 50-500px)";
        minimumZoneSizePx = Defaults::MinimumZoneSizePx;
    }
    m_minimumZoneSizePx = minimumZoneSizePx;

    int minimumZoneDisplaySizePx = zones.readEntry("MinimumZoneDisplaySizePx", Defaults::MinimumZoneDisplaySizePx);
    if (minimumZoneDisplaySizePx < 1 || minimumZoneDisplaySizePx > 50) {
        qCWarning(lcConfig) << "Invalid minimum zone display size:" << minimumZoneDisplaySizePx
                            << "using default (must be 1-50px)";
        minimumZoneDisplaySizePx = Defaults::MinimumZoneDisplaySizePx;
    }
    m_minimumZoneDisplaySizePx = minimumZoneDisplaySizePx;

    // Behavior
    m_keepWindowsInZonesOnResolutionChange = behavior.readEntry("KeepOnResolutionChange", true);
    m_moveNewWindowsToLastZone = behavior.readEntry("MoveNewToLastZone", false);
    m_restoreOriginalSizeOnUnsnap = behavior.readEntry("RestoreSizeOnUnsnap", true);
    int stickyHandling =
        behavior.readEntry("StickyWindowHandling", static_cast<int>(StickyWindowHandling::TreatAsNormal));
    m_stickyWindowHandling =
        static_cast<StickyWindowHandling>(qBound(static_cast<int>(StickyWindowHandling::TreatAsNormal), stickyHandling,
                                                 static_cast<int>(StickyWindowHandling::IgnoreAll)));
    m_defaultLayoutId = behavior.readEntry("DefaultLayoutId", QString());

    // Exclusions
    m_excludedApplications = exclusions.readEntry("Applications", QStringList());
    m_excludedWindowClasses = exclusions.readEntry("WindowClasses", QStringList());

    // Zone Selector
    KConfigGroup zoneSelector = config->group(QStringLiteral("ZoneSelector"));
    m_zoneSelectorEnabled = zoneSelector.readEntry("Enabled", true);
    int triggerDistance = zoneSelector.readEntry("TriggerDistance", 50);
    if (triggerDistance < 10 || triggerDistance > 200) {
        qCWarning(lcConfig) << "Invalid zone selector trigger distance:" << triggerDistance
                            << "using default (must be 10-200px)";
        triggerDistance = 50;
    }
    m_zoneSelectorTriggerDistance = triggerDistance;
    int selectorPos = zoneSelector.readEntry("Position", static_cast<int>(ZoneSelectorPosition::Top));
    // Valid positions are 0-8 except 4 (center)
    if (selectorPos >= 0 && selectorPos <= 8 && selectorPos != 4) {
        m_zoneSelectorPosition = static_cast<ZoneSelectorPosition>(selectorPos);
    } else {
        m_zoneSelectorPosition = ZoneSelectorPosition::Top;
    }
    int selectorMode = zoneSelector.readEntry("LayoutMode", static_cast<int>(ZoneSelectorLayoutMode::Grid));
    m_zoneSelectorLayoutMode = static_cast<ZoneSelectorLayoutMode>(
        qBound(0, selectorMode, static_cast<int>(ZoneSelectorLayoutMode::Vertical)));
    int previewWidth = zoneSelector.readEntry("PreviewWidth", 180);
    if (previewWidth < 80 || previewWidth > 400) {
        qCWarning(lcConfig) << "Invalid zone selector preview width:" << previewWidth << "using default (80-400px)";
        previewWidth = 180;
    }
    m_zoneSelectorPreviewWidth = previewWidth;
    int previewHeight = zoneSelector.readEntry("PreviewHeight", 101);
    if (previewHeight < 60 || previewHeight > 300) {
        qCWarning(lcConfig) << "Invalid zone selector preview height:" << previewHeight << "using default (60-300px)";
        previewHeight = 101;
    }
    m_zoneSelectorPreviewHeight = previewHeight;
    m_zoneSelectorPreviewLockAspect = zoneSelector.readEntry("PreviewLockAspect", true);
    int gridColumns = zoneSelector.readEntry("GridColumns", 3);
    if (gridColumns < 1 || gridColumns > 10) {
        qCWarning(lcConfig) << "Invalid zone selector grid columns:" << gridColumns << "using default (1-10)";
        gridColumns = 3;
    }
    m_zoneSelectorGridColumns = gridColumns;

    // Size mode (Auto/Manual)
    int sizeMode = zoneSelector.readEntry("SizeMode", static_cast<int>(ZoneSelectorSizeMode::Auto));
    m_zoneSelectorSizeMode =
        static_cast<ZoneSelectorSizeMode>(qBound(0, sizeMode, static_cast<int>(ZoneSelectorSizeMode::Manual)));

    // Max visible rows before scrolling (Auto mode)
    int maxRows = zoneSelector.readEntry("MaxRows", 4);
    if (maxRows < 1 || maxRows > 10) {
        qCWarning(lcConfig) << "Invalid zone selector max rows:" << maxRows << "using default (1-10)";
        maxRows = 4;
    }
    m_zoneSelectorMaxRows = maxRows;

    // Shader Effects
    KConfigGroup shaders = config->group(QStringLiteral("Shaders"));
    m_enableShaderEffects = shaders.readEntry("EnableShaderEffects", true);
    m_shaderFrameRate = qBound(30, shaders.readEntry("ShaderFrameRate", 60), 144);

    // Global Shortcuts
    KConfigGroup globalShortcuts = config->group(QStringLiteral("GlobalShortcuts"));
    m_openEditorShortcut = globalShortcuts.readEntry("OpenEditorShortcut", QStringLiteral("Meta+Shift+E"));
    m_previousLayoutShortcut = globalShortcuts.readEntry("PreviousLayoutShortcut", QStringLiteral("Meta+Alt+["));
    m_nextLayoutShortcut = globalShortcuts.readEntry("NextLayoutShortcut", QStringLiteral("Meta+Alt+]"));
    for (int i = 0; i < 9; ++i) {
        QString key = QStringLiteral("QuickLayout%1Shortcut").arg(i + 1);
        QString defaultShortcut = QStringLiteral("Meta+Alt+%1").arg(i + 1);
        m_quickLayoutShortcuts[i] = globalShortcuts.readEntry(key, defaultShortcut);
    }

    // Keyboard Navigation Shortcuts (Phase 1 features)
    // Shortcut pattern philosophy for consistency and KDE conflict avoidance:
    //   Meta+Alt+{key}         = Layout operations ([, ], 1-9, Return, Escape, F)
    //   Meta+Alt+Shift+Arrow   = Window zone movement
    //   Alt+Shift+Arrow        = Focus zone navigation (lighter action, no Meta)
    //   Meta+Ctrl+{1-9}        = Direct zone snapping
    // Meta+Shift+Left/Right conflicts with KDE's "Window to Next/Previous Screen";
    // we use Meta+Alt+Shift+Arrow instead.
    KConfigGroup navigationShortcuts = config->group(QStringLiteral("NavigationShortcuts"));
    m_moveWindowLeftShortcut = navigationShortcuts.readEntry("MoveWindowLeft", QStringLiteral("Meta+Alt+Shift+Left"));
    m_moveWindowRightShortcut =
        navigationShortcuts.readEntry("MoveWindowRight", QStringLiteral("Meta+Alt+Shift+Right"));
    m_moveWindowUpShortcut = navigationShortcuts.readEntry("MoveWindowUp", QStringLiteral("Meta+Alt+Shift+Up"));
    m_moveWindowDownShortcut = navigationShortcuts.readEntry("MoveWindowDown", QStringLiteral("Meta+Alt+Shift+Down"));
    // Meta+Arrow conflicts with KDE's Quick Tile; we use Alt+Shift+Arrow instead.
    m_focusZoneLeftShortcut = navigationShortcuts.readEntry("FocusZoneLeft", QStringLiteral("Alt+Shift+Left"));
    m_focusZoneRightShortcut = navigationShortcuts.readEntry("FocusZoneRight", QStringLiteral("Alt+Shift+Right"));
    m_focusZoneUpShortcut = navigationShortcuts.readEntry("FocusZoneUp", QStringLiteral("Alt+Shift+Up"));
    m_focusZoneDownShortcut = navigationShortcuts.readEntry("FocusZoneDown", QStringLiteral("Alt+Shift+Down"));
    m_pushToEmptyZoneShortcut = navigationShortcuts.readEntry("PushToEmptyZone", QStringLiteral("Meta+Alt+Return"));
    m_restoreWindowSizeShortcut = navigationShortcuts.readEntry("RestoreWindowSize", QStringLiteral("Meta+Alt+Escape"));
    m_toggleWindowFloatShortcut = navigationShortcuts.readEntry("ToggleWindowFloat", QStringLiteral("Meta+Alt+F"));

    // Snap to Zone by Number Shortcuts (Meta+Ctrl+1-9)
    for (int i = 0; i < 9; ++i) {
        QString key = QStringLiteral("SnapToZone%1").arg(i + 1);
        QString defaultShortcut = QStringLiteral("Meta+Ctrl+%1").arg(i + 1);
        m_snapToZoneShortcuts[i] = navigationShortcuts.readEntry(key, defaultShortcut);
    }

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
    behavior.writeEntry("DefaultLayoutId", m_defaultLayoutId);

    // Exclusions
    exclusions.writeEntry("Applications", m_excludedApplications);
    exclusions.writeEntry("WindowClasses", m_excludedWindowClasses);

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

    // Snap to Zone by Number Shortcuts
    for (int i = 0; i < 9; ++i) {
        QString key = QStringLiteral("SnapToZone%1").arg(i + 1);
        navigationShortcuts.writeEntry(key, m_snapToZoneShortcuts[i]);
    }

    config->sync();
}

void Settings::reset()
{
    // Reset to default values (DRY: use Defaults constants)
    m_shiftDragToActivate = true; // Deprecated
    m_dragActivationModifier = DragModifier::Alt; // Default: Alt for zone activation
    m_skipSnapModifier = DragModifier::Shift; // Default: Shift to skip snapping
    m_multiZoneModifier = DragModifier::CtrlAlt; // Default: Ctrl+Alt for multi-zone spanning
    m_middleClickMultiZone = true;

    m_showZonesOnAllMonitors = false;
    m_disabledMonitors.clear();
    m_showZoneNumbers = true;
    m_flashZonesOnSwitch = true;
    m_showOsdOnLayoutSwitch = true;

    // Appearance defaults from Defaults namespace (DRY)
    m_useSystemColors = true;
    m_highlightColor = Defaults::HighlightColor;
    m_inactiveColor = Defaults::InactiveColor;
    m_borderColor = Defaults::BorderColor;
    m_numberColor = Defaults::NumberColor;
    m_activeOpacity = Defaults::Opacity;
    m_inactiveOpacity = Defaults::InactiveOpacity;
    m_borderWidth = Defaults::BorderWidth;
    m_borderRadius = Defaults::BorderRadius;
    m_enableBlur = true;

    // Zone settings defaults (DRY)
    m_zonePadding = Defaults::ZonePadding;
    m_outerGap = Defaults::OuterGap;
    m_adjacentThreshold = Defaults::AdjacentThreshold;

    // Performance and behavior defaults (DRY)
    m_pollIntervalMs = Defaults::PollIntervalMs;
    m_minimumZoneSizePx = Defaults::MinimumZoneSizePx;
    m_minimumZoneDisplaySizePx = Defaults::MinimumZoneDisplaySizePx;

    m_keepWindowsInZonesOnResolutionChange = true;
    m_moveNewWindowsToLastZone = false;
    m_restoreOriginalSizeOnUnsnap = true;
    m_stickyWindowHandling = StickyWindowHandling::TreatAsNormal;
    m_defaultLayoutId.clear();

    m_excludedApplications.clear();
    m_excludedWindowClasses.clear();

    // Zone Selector defaults
    m_zoneSelectorEnabled = true;
    m_zoneSelectorTriggerDistance = 50;
    m_zoneSelectorPosition = ZoneSelectorPosition::Top;
    m_zoneSelectorLayoutMode = ZoneSelectorLayoutMode::Grid; // Grid is better UX for multiple layouts
    m_zoneSelectorPreviewWidth = 180;
    m_zoneSelectorPreviewHeight = 101;
    m_zoneSelectorPreviewLockAspect = true;
    m_zoneSelectorGridColumns = 3;
    m_zoneSelectorSizeMode = ZoneSelectorSizeMode::Auto; // Auto-calculate sizes from screen
    m_zoneSelectorMaxRows = 4; // Max visible rows before scrolling

    // Shader Effects defaults
    m_enableShaderEffects = true;
    m_shaderFrameRate = 60;

    // Global Shortcuts defaults
    m_openEditorShortcut = QStringLiteral("Meta+Shift+E");
    m_previousLayoutShortcut = QStringLiteral("Meta+Alt+[");
    m_nextLayoutShortcut = QStringLiteral("Meta+Alt+]");
    for (int i = 0; i < 9; ++i) {
        m_quickLayoutShortcuts[i] = QStringLiteral("Meta+Alt+%1").arg(i + 1);
    }

    // Keyboard Navigation Shortcuts defaults (Phase 1 features)
    // Shortcut pattern philosophy for consistency and KDE conflict avoidance:
    //   Meta+Alt+{key}         = Layout operations ([, ], 1-9, Return, Escape, F)
    //   Meta+Alt+Shift+Arrow   = Window zone movement
    //   Alt+Shift+Arrow        = Focus zone navigation (lighter action, no Meta)
    //   Meta+Ctrl+{1-9}        = Direct zone snapping
    // Meta+Shift+Left/Right conflicts with KDE's "Window to Next/Previous Screen";
    // we use Meta+Alt+Shift+Arrow instead.
    m_moveWindowLeftShortcut = QStringLiteral("Meta+Alt+Shift+Left");
    m_moveWindowRightShortcut = QStringLiteral("Meta+Alt+Shift+Right");
    m_moveWindowUpShortcut = QStringLiteral("Meta+Alt+Shift+Up");
    m_moveWindowDownShortcut = QStringLiteral("Meta+Alt+Shift+Down");
    // Meta+Arrow conflicts with KDE's Quick Tile; we use Alt+Shift+Arrow instead.
    m_focusZoneLeftShortcut = QStringLiteral("Alt+Shift+Left");
    m_focusZoneRightShortcut = QStringLiteral("Alt+Shift+Right");
    m_focusZoneUpShortcut = QStringLiteral("Alt+Shift+Up");
    m_focusZoneDownShortcut = QStringLiteral("Alt+Shift+Down");
    m_pushToEmptyZoneShortcut = QStringLiteral("Meta+Alt+Return");
    m_restoreWindowSizeShortcut = QStringLiteral("Meta+Alt+Escape");
    m_toggleWindowFloatShortcut = QStringLiteral("Meta+Alt+F");

    // Snap to Zone by Number Shortcuts
    for (int i = 0; i < 9; ++i) {
        m_snapToZoneShortcuts[i] = QStringLiteral("Meta+Ctrl+%1").arg(i + 1);
    }

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
