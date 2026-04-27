// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include "enums.h"
#include <PhosphorEngineApi/IGeometrySettings.h>
#include <QString>
#include <QStringList>
#include <QSet>
#include <QColor>
#include <QVariantList>
#include <QVariantMap>

namespace PlasmaZones {

/**
 * @brief Per-screen zone selector configuration
 *
 * Holds all zone selector settings that can be overridden per-monitor.
 * Used by OverlayService and ZoneSelectorController to apply resolved
 * (per-screen override > global default) settings for each screen.
 */
struct ZoneSelectorConfig
{
    int position = 1; // ZoneSelectorPosition enum value (Top)
    int layoutMode = 0; // ZoneSelectorLayoutMode enum value (Grid)
    int sizeMode = 0; // ZoneSelectorSizeMode enum value (Auto)
    int maxRows = 4;
    int previewWidth = 180;
    int previewHeight = 101;
    bool previewLockAspect = true;
    int gridColumns = 5;
    int triggerDistance = 50;
};

/**
 * @brief Config key names for per-screen zone selector overrides
 *
 * Used in KConfig group keys ([ZoneSelector:ScreenName]), QVariantMap
 * override storage, and QML writeSetting() calls.
 */
namespace ZoneSelectorConfigKey {
inline constexpr const char Position[] = "Position";
inline constexpr const char LayoutMode[] = "LayoutMode";
inline constexpr const char SizeMode[] = "SizeMode";
inline constexpr const char MaxRows[] = "MaxRows";
inline constexpr const char PreviewWidth[] = "PreviewWidth";
inline constexpr const char PreviewHeight[] = "PreviewHeight";
inline constexpr const char PreviewLockAspect[] = "PreviewLockAspect";
inline constexpr const char GridColumns[] = "GridColumns";
inline constexpr const char TriggerDistance[] = "TriggerDistance";
} // namespace ZoneSelectorConfigKey

/**
 * Per-screen autotile override key constants.
 * These intentionally differ from the global ConfigKeys accessors (e.g.
 * "AutotileAlgorithm" here vs ConfigKeys::defaultAutotileAlgorithmKey() =
 * "DefaultAutotileAlgorithm") because per-screen overrides replace the default.
 */
namespace PerScreenAutotileKey {
inline constexpr const char Algorithm[] = "AutotileAlgorithm";
inline constexpr const char SplitRatio[] = "AutotileSplitRatio";
inline constexpr const char MasterCount[] = "AutotileMasterCount";
inline constexpr const char InnerGap[] = "AutotileInnerGap";
inline constexpr const char OuterGap[] = "AutotileOuterGap";
inline constexpr const char UsePerSideOuterGap[] = "AutotileUsePerSideOuterGap";
inline constexpr const char OuterGapTop[] = "AutotileOuterGapTop";
inline constexpr const char OuterGapBottom[] = "AutotileOuterGapBottom";
inline constexpr const char OuterGapLeft[] = "AutotileOuterGapLeft";
inline constexpr const char OuterGapRight[] = "AutotileOuterGapRight";
inline constexpr const char FocusNewWindows[] = "AutotileFocusNewWindows";
inline constexpr const char SmartGaps[] = "AutotileSmartGaps";
inline constexpr const char MaxWindows[] = "AutotileMaxWindows";
inline constexpr const char InsertPosition[] = "AutotileInsertPosition";
inline constexpr const char FocusFollowsMouse[] = "AutotileFocusFollowsMouse";
inline constexpr const char RespectMinimumSize[] = "AutotileRespectMinimumSize";
inline constexpr const char HideTitleBars[] = "AutotileHideTitleBars";
inline constexpr const char SplitRatioStep[] = "AutotileSplitRatioStep";
inline constexpr const char AnimationsEnabled[] = "AnimationsEnabled";
inline constexpr const char AnimationDuration[] = "AnimationDuration";
inline constexpr const char AnimationEasingCurve[] = "AnimationEasingCurve";
} // namespace PerScreenAutotileKey

namespace PerScreenSnappingKey {

using PhosphorEngineApi::PerScreenSnappingKey::OuterGap;
using PhosphorEngineApi::PerScreenSnappingKey::OuterGapBottom;
using PhosphorEngineApi::PerScreenSnappingKey::OuterGapLeft;
using PhosphorEngineApi::PerScreenSnappingKey::OuterGapRight;
using PhosphorEngineApi::PerScreenSnappingKey::OuterGapTop;
using PhosphorEngineApi::PerScreenSnappingKey::UsePerSideOuterGap;
using PhosphorEngineApi::PerScreenSnappingKey::ZonePadding;

inline constexpr QLatin1String SnapAssistEnabled{"SnapAssistEnabled"};
inline constexpr QLatin1String ZoneSelectorEnabled{"ZoneSelectorEnabled"};
inline constexpr QLatin1String ZoneSelectorTriggerDistance{"ZoneSelectorTriggerDistance"};
inline constexpr QLatin1String ZoneSelectorPosition{"ZoneSelectorPosition"};
inline constexpr QLatin1String ZoneSelectorLayoutMode{"ZoneSelectorLayoutMode"};
inline constexpr QLatin1String ZoneSelectorSizeMode{"ZoneSelectorSizeMode"};
inline constexpr QLatin1String ZoneSelectorMaxRows{"ZoneSelectorMaxRows"};
inline constexpr QLatin1String ZoneSelectorPreviewWidth{"ZoneSelectorPreviewWidth"};
inline constexpr QLatin1String ZoneSelectorPreviewHeight{"ZoneSelectorPreviewHeight"};
} // namespace PerScreenSnappingKey

// ═══════════════════════════════════════════════════════════════════════════════
// Settings Interfaces
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

    virtual QVariantList dragActivationTriggers() const = 0;
    virtual void setDragActivationTriggers(const QVariantList& triggers) = 0;
    virtual bool zoneSpanEnabled() const = 0;
    virtual void setZoneSpanEnabled(bool enabled) = 0;
    virtual DragModifier zoneSpanModifier() const = 0;
    virtual void setZoneSpanModifier(DragModifier modifier) = 0;
    virtual QVariantList zoneSpanTriggers() const = 0;
    virtual void setZoneSpanTriggers(const QVariantList& triggers) = 0;
    virtual bool toggleActivation() const = 0;
    virtual void setToggleActivation(bool enable) = 0;
    virtual bool snappingEnabled() const = 0;
    virtual void setSnappingEnabled(bool enabled) = 0;
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
    virtual void setDisabledMonitors(const QStringList& screenIdOrNames) = 0;
    virtual bool isMonitorDisabled(const QString& screenIdOrName) const = 0;
    virtual QStringList disabledDesktops() const = 0;
    virtual void setDisabledDesktops(const QStringList& entries) = 0;
    virtual bool isDesktopDisabled(const QString& screenIdOrName, int desktop) const = 0;
    virtual QStringList disabledActivities() const = 0;
    virtual void setDisabledActivities(const QStringList& entries) = 0;
    virtual bool isActivityDisabled(const QString& screenIdOrName, const QString& activityId) const = 0;
    virtual QStringList lockedScreens() const = 0;
    virtual void setLockedScreens(const QStringList& screens) = 0;
    virtual void setScreenLocked(const QString& screenIdOrName, bool locked) = 0;
    virtual bool isScreenLocked(const QString& screenIdOrName) const = 0;
    virtual bool isContextLocked(const QString& screenIdOrName, int virtualDesktop, const QString& activity) const = 0;
    virtual void setContextLocked(const QString& screenIdOrName, int virtualDesktop, const QString& activity,
                                  bool locked) = 0;
    virtual bool showZoneNumbers() const = 0;
    virtual void setShowZoneNumbers(bool show) = 0;
    virtual bool flashZonesOnSwitch() const = 0;
    virtual void setFlashZonesOnSwitch(bool flash) = 0;
    virtual bool showOsdOnLayoutSwitch() const = 0;
    virtual void setShowOsdOnLayoutSwitch(bool show) = 0;
    virtual bool showOsdOnDesktopSwitch() const = 0;
    virtual void setShowOsdOnDesktopSwitch(bool show) = 0;
    virtual bool showNavigationOsd() const = 0;
    virtual void setShowNavigationOsd(bool show) = 0;
    virtual OsdStyle osdStyle() const = 0;
    virtual void setOsdStyle(OsdStyle style) = 0;
    virtual OverlayDisplayMode overlayDisplayMode() const = 0;
    virtual void setOverlayDisplayMode(OverlayDisplayMode mode) = 0;

    // Appearance settings
    virtual bool useSystemColors() const = 0;
    virtual void setUseSystemColors(bool use) = 0;
    virtual QColor highlightColor() const = 0;
    virtual void setHighlightColor(const QColor& color) = 0;
    virtual QColor inactiveColor() const = 0;
    virtual void setInactiveColor(const QColor& color) = 0;
    virtual QColor borderColor() const = 0;
    virtual void setBorderColor(const QColor& color) = 0;
    virtual QColor labelFontColor() const = 0;
    virtual void setLabelFontColor(const QColor& color) = 0;
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

    // Label font settings
    virtual QString labelFontFamily() const = 0;
    virtual void setLabelFontFamily(const QString& family) = 0;
    virtual qreal labelFontSizeScale() const = 0;
    virtual void setLabelFontSizeScale(qreal scale) = 0;
    virtual int labelFontWeight() const = 0;
    virtual void setLabelFontWeight(int weight) = 0;
    virtual bool labelFontItalic() const = 0;
    virtual void setLabelFontItalic(bool italic) = 0;
    virtual bool labelFontUnderline() const = 0;
    virtual void setLabelFontUnderline(bool underline) = 0;
    virtual bool labelFontStrikeout() const = 0;
    virtual void setLabelFontStrikeout(bool strikeout) = 0;

    // Shader effects
    virtual bool enableShaderEffects() const = 0;
    virtual void setEnableShaderEffects(bool enable) = 0;
    virtual int shaderFrameRate() const = 0;
    virtual void setShaderFrameRate(int fps) = 0;
    virtual bool enableAudioVisualizer() const = 0;
    virtual void setEnableAudioVisualizer(bool enable) = 0;
    virtual int audioSpectrumBarCount() const = 0;
    virtual void setAudioSpectrumBarCount(int count) = 0;
};

/**
 * @brief Settings related to zone geometry (padding, gaps, thresholds)
 *
 * Used by: KWin Effect, KCM, PhosphorZones::Zone Detector
 *
 * Per-side gap semantics differ by implementer:
 * - Settings: values are clamped to [0, INT_MAX]; -1 is not valid (global config always has a value)
 * - PhosphorZones::Layout: -1 sentinel means "use global setting"; >= 0 is a per-layout override
 */
class PLASMAZONES_EXPORT IZoneGeometrySettings : public PhosphorEngineApi::IGeometrySettings
{
public:
    ~IZoneGeometrySettings() override = default;

    virtual void setZonePadding(int padding) = 0;
    virtual void setOuterGap(int gap) = 0;
    virtual void setUsePerSideOuterGap(bool enabled) = 0;
    virtual void setOuterGapTop(int gap) = 0;
    virtual void setOuterGapBottom(int gap) = 0;
    virtual void setOuterGapLeft(int gap) = 0;
    virtual void setOuterGapRight(int gap) = 0;
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

    // Per-screen zone selector config resolution
    virtual ZoneSelectorConfig resolvedZoneSelectorConfig(const QString& /*screenIdOrName*/) const
    {
        return {static_cast<int>(zoneSelectorPosition()),
                static_cast<int>(zoneSelectorLayoutMode()),
                static_cast<int>(zoneSelectorSizeMode()),
                zoneSelectorMaxRows(),
                zoneSelectorPreviewWidth(),
                zoneSelectorPreviewHeight(),
                zoneSelectorPreviewLockAspect(),
                zoneSelectorGridColumns(),
                zoneSelectorTriggerDistance()};
    }
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
    virtual bool restoreWindowsToZonesOnLogin() const = 0;
    virtual void setRestoreWindowsToZonesOnLogin(bool restore) = 0;
    virtual bool autoAssignAllLayouts() const = 0;
    virtual void setAutoAssignAllLayouts(bool enabled) = 0;
    virtual bool snapAssistFeatureEnabled() const = 0;
    virtual void setSnapAssistFeatureEnabled(bool enabled) = 0;
    virtual bool snapAssistEnabled() const = 0;
    virtual void setSnapAssistEnabled(bool enabled) = 0;
    virtual QVariantList snapAssistTriggers() const = 0;
    virtual void setSnapAssistTriggers(const QVariantList& triggers) = 0;

    virtual bool filterLayoutsByAspectRatio() const = 0;
    virtual void setFilterLayoutsByAspectRatio(bool filter) = 0;
};

/**
 * @brief Settings related to manual layout/algorithm ordering
 *
 * Used by: Daemon (layout cycling, zone selector, overlay), Settings UI
 */
class PLASMAZONES_EXPORT IOrderingSettings
{
public:
    virtual ~IOrderingSettings() = default;

    virtual QStringList snappingLayoutOrder() const = 0;
    virtual void setSnappingLayoutOrder(const QStringList& order) = 0;
    virtual QStringList tilingAlgorithmOrder() const = 0;
    virtual void setTilingAlgorithmOrder(const QStringList& order) = 0;
};

/**
 * @brief Settings related to default layout selection
 *
 * Used by: Daemon, KCM, PhosphorZones::Layout Manager
 */
class PLASMAZONES_EXPORT IDefaultLayoutSettings
{
public:
    virtual ~IDefaultLayoutSettings() = default;

    virtual QString defaultLayoutId() const = 0;
    virtual void setDefaultLayoutId(const QString& layoutId) = 0;
};

/**
 * @brief Why PlasmaZones is disabled in a given context.
 *
 * Priority order: Monitor > Desktop > Activity (matches isContextDisabled check order).
 */
enum class DisabledReason {
    NotDisabled,
    MonitorDisabled,
    DesktopDisabled,
    ActivityDisabled
};

/**
 * @brief Determine *why* PlasmaZones is disabled for a given screen/desktop/activity.
 *
 * Returns the highest-priority reason, or NotDisabled if the context is active.
 * Use this when you need the reason (e.g. for OSD messages); use isContextDisabled()
 * for simple gate checks.
 */
inline DisabledReason contextDisabledReason(const IZoneVisualizationSettings* s, const QString& screenId, int desktop,
                                            const QString& activity)
{
    if (!s)
        return DisabledReason::NotDisabled;
    if (s->isMonitorDisabled(screenId))
        return DisabledReason::MonitorDisabled;
    if (desktop > 0 && s->isDesktopDisabled(screenId, desktop))
        return DisabledReason::DesktopDisabled;
    if (!activity.isEmpty() && s->isActivityDisabled(screenId, activity))
        return DisabledReason::ActivityDisabled;
    return DisabledReason::NotDisabled;
}

/**
 * @brief Check if PlasmaZones is disabled for a given screen/desktop/activity context.
 *
 * Returns true if the screen is disabled, OR the desktop is disabled, OR the activity
 * is disabled. Use at every point where overlay/snapping/autotile is gated.
 */
inline bool isContextDisabled(const IZoneVisualizationSettings* s, const QString& screenId, int desktop,
                              const QString& activity)
{
    return contextDisabledReason(s, screenId, desktop, activity) != DisabledReason::NotDisabled;
}

/**
 * @brief Remove disabled-desktop entries whose desktop number exceeds @p maxDesktop.
 * @return true if any entries were removed.
 *
 * Composite key format: "screenId/desktopNumber". Malformed entries are also removed.
 */
inline bool pruneDisabledDesktopEntries(QStringList& entries, int maxDesktop)
{
    const int before = entries.size();
    entries.removeIf([maxDesktop](const QString& entry) {
        // Composite key format: "screenId/desktopNumber". Virtual screen IDs
        // (e.g. "physId/vs:N") contain '/', but the desktop suffix is always the
        // last segment, so lastIndexOf('/') correctly splits at the boundary.
        int slashIdx = entry.lastIndexOf(QLatin1Char('/'));
        if (slashIdx < 0)
            return true; // malformed entry
        bool ok = false;
        int d = entry.mid(slashIdx + 1).toInt(&ok);
        return !ok || d > maxDesktop;
    });
    return entries.size() != before;
}

/**
 * @brief Remove disabled-activity entries whose activity UUID is not in @p validActivityIds.
 * @return true if any entries were removed.
 *
 * Composite key format: "screenId/activityUuid". Malformed entries are also removed.
 */
inline bool pruneDisabledActivityEntries(QStringList& entries, const QSet<QString>& validActivityIds)
{
    const int before = entries.size();
    entries.removeIf([&validActivityIds](const QString& entry) {
        // Composite key format: "screenId/activityUuid". Virtual screen IDs
        // (e.g. "physId/vs:N") contain '/', but the activity suffix is always the
        // last segment, so lastIndexOf('/') correctly splits at the boundary.
        int slashIdx = entry.lastIndexOf(QLatin1Char('/'));
        if (slashIdx < 0)
            return true; // malformed entry
        QString actId = entry.mid(slashIdx + 1);
        return !validActivityIds.contains(actId);
    });
    return entries.size() != before;
}

} // namespace PlasmaZones
