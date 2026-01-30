// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QString>
#include <QStringList>
#include <QColor>

namespace PlasmaZones {

// Forward declarations for enums defined in interfaces.h
enum class DragModifier;
enum class ZoneSelectorPosition;
enum class ZoneSelectorLayoutMode;
enum class ZoneSelectorSizeMode;
enum class StickyWindowHandling;
enum class OsdStyle;

// ═══════════════════════════════════════════════════════════════════════════════
// Interface Segregation Principle (ISP) - Settings Interfaces
// ═══════════════════════════════════════════════════════════════════════════════
// These interfaces allow components to depend only on the settings they need,
// rather than the full ISettings interface. This reduces coupling and makes
// testing easier.
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * @brief Settings related to zone activation (drag modifiers, triggers)
 *
 * Used by: KWin Effect, KCM
 */
class PLASMAZONES_EXPORT IZoneActivationSettings
{
public:
    virtual ~IZoneActivationSettings() = default;

    virtual bool shiftDragToActivate() const = 0;
    virtual void setShiftDragToActivate(bool enable) = 0;
    virtual DragModifier dragActivationModifier() const = 0;
    virtual void setDragActivationModifier(DragModifier modifier) = 0;
    virtual DragModifier skipSnapModifier() const = 0;
    virtual void setSkipSnapModifier(DragModifier modifier) = 0;
    virtual DragModifier multiZoneModifier() const = 0;
    virtual void setMultiZoneModifier(DragModifier modifier) = 0;
    virtual bool middleClickMultiZone() const = 0;
    virtual void setMiddleClickMultiZone(bool enable) = 0;
};

/**
 * @brief Settings related to zone visualization (colors, opacity, blur)
 *
 * Used by: KWin Effect, KCM, Overlay Service
 */
class PLASMAZONES_EXPORT IZoneVisualizationSettings
{
public:
    virtual ~IZoneVisualizationSettings() = default;

    // Display settings
    virtual bool showZonesOnAllMonitors() const = 0;
    virtual void setShowZonesOnAllMonitors(bool show) = 0;
    virtual QStringList disabledMonitors() const = 0;
    virtual void setDisabledMonitors(const QStringList& screenNames) = 0;
    virtual bool isMonitorDisabled(const QString& screenName) const = 0;
    virtual bool showZoneNumbers() const = 0;
    virtual void setShowZoneNumbers(bool show) = 0;
    virtual bool flashZonesOnSwitch() const = 0;
    virtual void setFlashZonesOnSwitch(bool flash) = 0;
    virtual bool showOsdOnLayoutSwitch() const = 0;
    virtual void setShowOsdOnLayoutSwitch(bool show) = 0;
    virtual bool showNavigationOsd() const = 0;
    virtual void setShowNavigationOsd(bool show) = 0;
    virtual OsdStyle osdStyle() const = 0;
    virtual void setOsdStyle(OsdStyle style) = 0;

    // Appearance settings
    virtual bool useSystemColors() const = 0;
    virtual void setUseSystemColors(bool use) = 0;
    virtual QColor highlightColor() const = 0;
    virtual void setHighlightColor(const QColor& color) = 0;
    virtual QColor inactiveColor() const = 0;
    virtual void setInactiveColor(const QColor& color) = 0;
    virtual QColor borderColor() const = 0;
    virtual void setBorderColor(const QColor& color) = 0;
    virtual QColor numberColor() const = 0;
    virtual void setNumberColor(const QColor& color) = 0;
    virtual qreal activeOpacity() const = 0;
    virtual void setActiveOpacity(qreal opacity) = 0;
    virtual qreal inactiveOpacity() const = 0;
    virtual void setInactiveOpacity(qreal opacity) = 0;
    virtual int borderWidth() const = 0;
    virtual void setBorderWidth(int width) = 0;
    virtual int borderRadius() const = 0;
    virtual void setBorderRadius(int radius) = 0;
    virtual bool enableBlur() const = 0;
    virtual void setEnableBlur(bool enable) = 0;

    // Shader effects
    virtual bool enableShaderEffects() const = 0;
    virtual void setEnableShaderEffects(bool enable) = 0;
    virtual int shaderFrameRate() const = 0;
    virtual void setShaderFrameRate(int fps) = 0;
};

/**
 * @brief Settings related to zone geometry (padding, gaps, thresholds)
 *
 * Used by: KWin Effect, KCM, Zone Detector
 */
class PLASMAZONES_EXPORT IZoneGeometrySettings
{
public:
    virtual ~IZoneGeometrySettings() = default;

    virtual int zonePadding() const = 0;
    virtual void setZonePadding(int padding) = 0;
    virtual int outerGap() const = 0;
    virtual void setOuterGap(int gap) = 0;
    virtual int adjacentThreshold() const = 0;
    virtual void setAdjacentThreshold(int threshold) = 0;
    virtual int pollIntervalMs() const = 0;
    virtual void setPollIntervalMs(int interval) = 0;
    virtual int minimumZoneSizePx() const = 0;
    virtual void setMinimumZoneSizePx(int size) = 0;
    virtual int minimumZoneDisplaySizePx() const = 0;
    virtual void setMinimumZoneDisplaySizePx(int size) = 0;
};

/**
 * @brief Settings related to window exclusion rules
 *
 * Used by: KWin Effect only (not exposed in KCM visualization)
 */
class PLASMAZONES_EXPORT IWindowExclusionSettings
{
public:
    virtual ~IWindowExclusionSettings() = default;

    virtual QStringList excludedApplications() const = 0;
    virtual void setExcludedApplications(const QStringList& apps) = 0;
    virtual QStringList excludedWindowClasses() const = 0;
    virtual void setExcludedWindowClasses(const QStringList& classes) = 0;
    virtual bool excludeTransientWindows() const = 0;
    virtual void setExcludeTransientWindows(bool exclude) = 0;
    virtual int minimumWindowWidth() const = 0;
    virtual void setMinimumWindowWidth(int width) = 0;
    virtual int minimumWindowHeight() const = 0;
    virtual void setMinimumWindowHeight(int height) = 0;
    virtual bool isWindowExcluded(const QString& appName, const QString& windowClass) const = 0;
};

/**
 * @brief Settings related to the zone selector UI
 *
 * Used by: KWin Effect, KCM, Overlay Service
 */
class PLASMAZONES_EXPORT IZoneSelectorSettings
{
public:
    virtual ~IZoneSelectorSettings() = default;

    virtual bool zoneSelectorEnabled() const = 0;
    virtual void setZoneSelectorEnabled(bool enabled) = 0;
    virtual int zoneSelectorTriggerDistance() const = 0;
    virtual void setZoneSelectorTriggerDistance(int distance) = 0;
    virtual ZoneSelectorPosition zoneSelectorPosition() const = 0;
    virtual void setZoneSelectorPosition(ZoneSelectorPosition position) = 0;
    virtual ZoneSelectorLayoutMode zoneSelectorLayoutMode() const = 0;
    virtual void setZoneSelectorLayoutMode(ZoneSelectorLayoutMode mode) = 0;
    virtual int zoneSelectorPreviewWidth() const = 0;
    virtual void setZoneSelectorPreviewWidth(int width) = 0;
    virtual int zoneSelectorPreviewHeight() const = 0;
    virtual void setZoneSelectorPreviewHeight(int height) = 0;
    virtual bool zoneSelectorPreviewLockAspect() const = 0;
    virtual void setZoneSelectorPreviewLockAspect(bool locked) = 0;
    virtual int zoneSelectorGridColumns() const = 0;
    virtual void setZoneSelectorGridColumns(int columns) = 0;
    virtual ZoneSelectorSizeMode zoneSelectorSizeMode() const = 0;
    virtual void setZoneSelectorSizeMode(ZoneSelectorSizeMode mode) = 0;
    virtual int zoneSelectorMaxRows() const = 0;
    virtual void setZoneSelectorMaxRows(int rows) = 0;
};

/**
 * @brief Settings related to window behavior (snap restore, sticky handling)
 *
 * Used by: KWin Effect, KCM, Window Tracking Service
 */
class PLASMAZONES_EXPORT IWindowBehaviorSettings
{
public:
    virtual ~IWindowBehaviorSettings() = default;

    virtual bool keepWindowsInZonesOnResolutionChange() const = 0;
    virtual void setKeepWindowsInZonesOnResolutionChange(bool keep) = 0;
    virtual bool moveNewWindowsToLastZone() const = 0;
    virtual void setMoveNewWindowsToLastZone(bool move) = 0;
    virtual bool restoreOriginalSizeOnUnsnap() const = 0;
    virtual void setRestoreOriginalSizeOnUnsnap(bool restore) = 0;
    virtual StickyWindowHandling stickyWindowHandling() const = 0;
    virtual void setStickyWindowHandling(StickyWindowHandling handling) = 0;
};

/**
 * @brief Settings related to default layout selection
 *
 * Used by: Daemon, KCM, Layout Manager
 */
class PLASMAZONES_EXPORT IDefaultLayoutSettings
{
public:
    virtual ~IDefaultLayoutSettings() = default;

    virtual QString defaultLayoutId() const = 0;
    virtual void setDefaultLayoutId(const QString& layoutId) = 0;
};

/**
 * @brief Settings related to autotiling behavior
 *
 * Used by: KWin Effect, Autotile Engine, KCM
 */
class PLASMAZONES_EXPORT IAutotileSettings
{
public:
    virtual ~IAutotileSettings() = default;

    // Core autotile settings
    virtual bool autotileEnabled() const = 0;
    virtual void setAutotileEnabled(bool enabled) = 0;
    virtual QString autotileAlgorithm() const = 0;
    virtual void setAutotileAlgorithm(const QString& algorithm) = 0;
    virtual qreal autotileSplitRatio() const = 0;
    virtual void setAutotileSplitRatio(qreal ratio) = 0;
    virtual int autotileMasterCount() const = 0;
    virtual void setAutotileMasterCount(int count) = 0;
    virtual int autotileInnerGap() const = 0;
    virtual void setAutotileInnerGap(int gap) = 0;
    virtual int autotileOuterGap() const = 0;
    virtual void setAutotileOuterGap(int gap) = 0;
    virtual bool autotileFocusNewWindows() const = 0;
    virtual void setAutotileFocusNewWindows(bool focus) = 0;
    virtual bool autotileSmartGaps() const = 0;
    virtual void setAutotileSmartGaps(bool smart) = 0;

    // Animation settings (KWin effect visual transitions)
    virtual bool autotileAnimationsEnabled() const = 0;
    virtual void setAutotileAnimationsEnabled(bool enabled) = 0;
    virtual int autotileAnimationDuration() const = 0;
    virtual void setAutotileAnimationDuration(int duration) = 0;

    // Additional autotile settings
    virtual bool autotileFocusFollowsMouse() const = 0;
    virtual void setAutotileFocusFollowsMouse(bool focus) = 0;
    virtual bool autotileRespectMinimumSize() const = 0;
    virtual void setAutotileRespectMinimumSize(bool respect) = 0;
    virtual bool autotileShowActiveBorder() const = 0;
    virtual void setAutotileShowActiveBorder(bool show) = 0;
    virtual int autotileActiveBorderWidth() const = 0;
    virtual void setAutotileActiveBorderWidth(int width) = 0;
    virtual bool autotileUseSystemBorderColor() const = 0;
    virtual void setAutotileUseSystemBorderColor(bool use) = 0;
    virtual QColor autotileActiveBorderColor() const = 0;
    virtual void setAutotileActiveBorderColor(const QColor& color) = 0;
    virtual bool autotileMonocleHideOthers() const = 0;
    virtual void setAutotileMonocleHideOthers(bool hide) = 0;
    virtual bool autotileMonocleShowTabs() const = 0;
    virtual void setAutotileMonocleShowTabs(bool show) = 0;
};

} // namespace PlasmaZones
