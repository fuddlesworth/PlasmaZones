// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/interfaces.h"

namespace PlasmaZones {

/**
 * @brief Unified stub ISettings for unit tests
 *
 * Provides sensible defaults for all ISettings pure virtual methods.
 * The defaultLayoutId can be overridden via setTestDefaultLayoutId().
 */
class StubSettings : public ISettings
{
    // No Q_OBJECT — this stub has no signals/slots of its own.
    // ISettings::Q_OBJECT provides the meta-object system integration.

public:
    explicit StubSettings(QObject* parent = nullptr)
        : ISettings(parent)
    {
    }

    // IDefaultLayoutSettings
    QString defaultLayoutId() const override
    {
        return m_defaultLayoutId;
    }
    void setDefaultLayoutId(const QString& id) override
    {
        m_defaultLayoutId = id;
    }
    void setTestDefaultLayoutId(const QString& id)
    {
        m_defaultLayoutId = id;
    }

    // IZoneActivationSettings
    bool shiftDragToActivate() const override
    {
        return false;
    }
    void setShiftDragToActivate(bool) override
    {
    }
    bool snappingEnabled() const override
    {
        return true;
    }
    void setSnappingEnabled(bool) override
    {
    }
    QVariantList dragActivationTriggers() const override
    {
        return {};
    }
    void setDragActivationTriggers(const QVariantList&) override
    {
    }
    bool zoneSpanEnabled() const override
    {
        return false;
    }
    void setZoneSpanEnabled(bool) override
    {
    }
    DragModifier zoneSpanModifier() const override
    {
        return DragModifier::Disabled;
    }
    void setZoneSpanModifier(DragModifier) override
    {
    }
    QVariantList zoneSpanTriggers() const override
    {
        return {};
    }
    void setZoneSpanTriggers(const QVariantList&) override
    {
    }
    bool toggleActivation() const override
    {
        return false;
    }
    void setToggleActivation(bool) override
    {
    }

    // IZoneVisualizationSettings
    bool showZonesOnAllMonitors() const override
    {
        return false;
    }
    void setShowZonesOnAllMonitors(bool) override
    {
    }
    QStringList disabledMonitors() const override
    {
        return {};
    }
    void setDisabledMonitors(const QStringList&) override
    {
    }
    bool isMonitorDisabled(const QString&) const override
    {
        return false;
    }
    QList<int> disabledDesktops() const override
    {
        return {};
    }
    void setDisabledDesktops(const QList<int>&) override
    {
    }
    bool isDesktopDisabled(int) const override
    {
        return false;
    }
    QStringList disabledActivities() const override
    {
        return {};
    }
    void setDisabledActivities(const QStringList&) override
    {
    }
    bool isActivityDisabled(const QString&) const override
    {
        return false;
    }
    bool showZoneNumbers() const override
    {
        return true;
    }
    void setShowZoneNumbers(bool) override
    {
    }
    bool flashZonesOnSwitch() const override
    {
        return false;
    }
    void setFlashZonesOnSwitch(bool) override
    {
    }
    bool showOsdOnLayoutSwitch() const override
    {
        return false;
    }
    void setShowOsdOnLayoutSwitch(bool) override
    {
    }
    bool showNavigationOsd() const override
    {
        return false;
    }
    void setShowNavigationOsd(bool) override
    {
    }
    OsdStyle osdStyle() const override
    {
        return OsdStyle::None;
    }
    void setOsdStyle(OsdStyle) override
    {
    }
    OverlayDisplayMode overlayDisplayMode() const override
    {
        return OverlayDisplayMode::ZoneRectangles;
    }
    void setOverlayDisplayMode(OverlayDisplayMode) override
    {
    }
    bool useSystemColors() const override
    {
        return false;
    }
    void setUseSystemColors(bool) override
    {
    }
    QColor highlightColor() const override
    {
        return Qt::blue;
    }
    void setHighlightColor(const QColor&) override
    {
    }
    QColor inactiveColor() const override
    {
        return Qt::gray;
    }
    void setInactiveColor(const QColor&) override
    {
    }
    QColor borderColor() const override
    {
        return Qt::white;
    }
    void setBorderColor(const QColor&) override
    {
    }
    QColor labelFontColor() const override
    {
        return Qt::white;
    }
    void setLabelFontColor(const QColor&) override
    {
    }
    qreal activeOpacity() const override
    {
        return 0.5;
    }
    void setActiveOpacity(qreal) override
    {
    }
    qreal inactiveOpacity() const override
    {
        return 0.3;
    }
    void setInactiveOpacity(qreal) override
    {
    }
    int borderWidth() const override
    {
        return 2;
    }
    void setBorderWidth(int) override
    {
    }
    int borderRadius() const override
    {
        return 0;
    }
    void setBorderRadius(int) override
    {
    }
    bool enableBlur() const override
    {
        return false;
    }
    void setEnableBlur(bool) override
    {
    }
    QString labelFontFamily() const override
    {
        return {};
    }
    void setLabelFontFamily(const QString&) override
    {
    }
    qreal labelFontSizeScale() const override
    {
        return 1.0;
    }
    void setLabelFontSizeScale(qreal) override
    {
    }
    int labelFontWeight() const override
    {
        return 400;
    }
    void setLabelFontWeight(int) override
    {
    }
    bool labelFontItalic() const override
    {
        return false;
    }
    void setLabelFontItalic(bool) override
    {
    }
    bool labelFontUnderline() const override
    {
        return false;
    }
    void setLabelFontUnderline(bool) override
    {
    }
    bool labelFontStrikeout() const override
    {
        return false;
    }
    void setLabelFontStrikeout(bool) override
    {
    }
    bool enableShaderEffects() const override
    {
        return false;
    }
    void setEnableShaderEffects(bool) override
    {
    }
    int shaderFrameRate() const override
    {
        return 60;
    }
    void setShaderFrameRate(int) override
    {
    }
    bool enableAudioVisualizer() const override
    {
        return false;
    }
    void setEnableAudioVisualizer(bool) override
    {
    }
    int audioSpectrumBarCount() const override
    {
        return 32;
    }
    void setAudioSpectrumBarCount(int) override
    {
    }

    // IZoneGeometrySettings
    int zonePadding() const override
    {
        return 8;
    }
    void setZonePadding(int) override
    {
    }
    int outerGap() const override
    {
        return 8;
    }
    void setOuterGap(int) override
    {
    }
    bool usePerSideOuterGap() const override
    {
        return false;
    }
    void setUsePerSideOuterGap(bool) override
    {
    }
    int outerGapTop() const override
    {
        return 8;
    }
    void setOuterGapTop(int) override
    {
    }
    int outerGapBottom() const override
    {
        return 8;
    }
    void setOuterGapBottom(int) override
    {
    }
    int outerGapLeft() const override
    {
        return 8;
    }
    void setOuterGapLeft(int) override
    {
    }
    int outerGapRight() const override
    {
        return 8;
    }
    void setOuterGapRight(int) override
    {
    }
    int adjacentThreshold() const override
    {
        return 20;
    }
    void setAdjacentThreshold(int) override
    {
    }
    int pollIntervalMs() const override
    {
        return 16;
    }
    void setPollIntervalMs(int) override
    {
    }
    int minimumZoneSizePx() const override
    {
        return 50;
    }
    void setMinimumZoneSizePx(int) override
    {
    }
    int minimumZoneDisplaySizePx() const override
    {
        return 100;
    }
    void setMinimumZoneDisplaySizePx(int) override
    {
    }

    // IWindowExclusionSettings
    QStringList excludedApplications() const override
    {
        return {};
    }
    void setExcludedApplications(const QStringList&) override
    {
    }
    QStringList excludedWindowClasses() const override
    {
        return {};
    }
    void setExcludedWindowClasses(const QStringList&) override
    {
    }
    bool excludeTransientWindows() const override
    {
        return true;
    }
    void setExcludeTransientWindows(bool) override
    {
    }
    int minimumWindowWidth() const override
    {
        return 0;
    }
    void setMinimumWindowWidth(int) override
    {
    }
    int minimumWindowHeight() const override
    {
        return 0;
    }
    void setMinimumWindowHeight(int) override
    {
    }
    // IZoneSelectorSettings
    bool zoneSelectorEnabled() const override
    {
        return false;
    }
    void setZoneSelectorEnabled(bool) override
    {
    }
    int zoneSelectorTriggerDistance() const override
    {
        return 20;
    }
    void setZoneSelectorTriggerDistance(int) override
    {
    }
    ZoneSelectorPosition zoneSelectorPosition() const override
    {
        return ZoneSelectorPosition::TopLeft;
    }
    void setZoneSelectorPosition(ZoneSelectorPosition) override
    {
    }
    ZoneSelectorLayoutMode zoneSelectorLayoutMode() const override
    {
        return ZoneSelectorLayoutMode::Grid;
    }
    void setZoneSelectorLayoutMode(ZoneSelectorLayoutMode) override
    {
    }
    int zoneSelectorPreviewWidth() const override
    {
        return 200;
    }
    void setZoneSelectorPreviewWidth(int) override
    {
    }
    int zoneSelectorPreviewHeight() const override
    {
        return 150;
    }
    void setZoneSelectorPreviewHeight(int) override
    {
    }
    bool zoneSelectorPreviewLockAspect() const override
    {
        return false;
    }
    void setZoneSelectorPreviewLockAspect(bool) override
    {
    }
    int zoneSelectorGridColumns() const override
    {
        return 3;
    }
    void setZoneSelectorGridColumns(int) override
    {
    }
    ZoneSelectorSizeMode zoneSelectorSizeMode() const override
    {
        return ZoneSelectorSizeMode::Auto;
    }
    void setZoneSelectorSizeMode(ZoneSelectorSizeMode) override
    {
    }
    int zoneSelectorMaxRows() const override
    {
        return 3;
    }
    void setZoneSelectorMaxRows(int) override
    {
    }

    // IWindowBehaviorSettings
    bool keepWindowsInZonesOnResolutionChange() const override
    {
        return false;
    }
    void setKeepWindowsInZonesOnResolutionChange(bool) override
    {
    }
    bool moveNewWindowsToLastZone() const override
    {
        return false;
    }
    void setMoveNewWindowsToLastZone(bool) override
    {
    }
    bool restoreOriginalSizeOnUnsnap() const override
    {
        return true;
    }
    void setRestoreOriginalSizeOnUnsnap(bool) override
    {
    }
    StickyWindowHandling stickyWindowHandling() const override
    {
        return StickyWindowHandling::TreatAsNormal;
    }
    void setStickyWindowHandling(StickyWindowHandling) override
    {
    }
    bool restoreWindowsToZonesOnLogin() const override
    {
        return false;
    }
    void setRestoreWindowsToZonesOnLogin(bool) override
    {
    }
    bool snapAssistFeatureEnabled() const override
    {
        return false;
    }
    void setSnapAssistFeatureEnabled(bool) override
    {
    }
    bool snapAssistEnabled() const override
    {
        return false;
    }
    void setSnapAssistEnabled(bool) override
    {
    }
    QVariantList snapAssistTriggers() const override
    {
        return {};
    }
    void setSnapAssistTriggers(const QVariantList&) override
    {
    }
    bool filterLayoutsByAspectRatio() const override
    {
        return true;
    }
    void setFilterLayoutsByAspectRatio(bool) override
    {
    }

    // Animation settings (ISettings)
    bool animationsEnabled() const override
    {
        return false;
    }
    void setAnimationsEnabled(bool) override
    {
    }
    int animationDuration() const override
    {
        return 200;
    }
    void setAnimationDuration(int) override
    {
    }
    QString animationEasingCurve() const override
    {
        return {};
    }
    void setAnimationEasingCurve(const QString&) override
    {
    }
    int animationMinDistance() const override
    {
        return 0;
    }
    void setAnimationMinDistance(int) override
    {
    }
    int animationSequenceMode() const override
    {
        return 0;
    }
    void setAnimationSequenceMode(int) override
    {
    }
    int animationStaggerInterval() const override
    {
        return 0;
    }
    void setAnimationStaggerInterval(int) override
    {
    }

    // Autotile decoration settings (ISettings)
    bool autotileFocusFollowsMouse() const override
    {
        return false;
    }
    void setAutotileFocusFollowsMouse(bool) override
    {
    }
    bool autotileHideTitleBars() const override
    {
        return false;
    }
    void setAutotileHideTitleBars(bool) override
    {
    }
    bool autotileShowBorder() const override
    {
        return false;
    }
    void setAutotileShowBorder(bool) override
    {
    }
    int autotileBorderWidth() const override
    {
        return 2;
    }
    void setAutotileBorderWidth(int) override
    {
    }
    int autotileBorderRadius() const override
    {
        return 0;
    }
    void setAutotileBorderRadius(int) override
    {
    }
    QColor autotileBorderColor() const override
    {
        return {};
    }
    void setAutotileBorderColor(const QColor&) override
    {
    }
    QColor autotileInactiveBorderColor() const override
    {
        return {};
    }
    void setAutotileInactiveBorderColor(const QColor&) override
    {
    }
    bool autotileUseSystemBorderColors() const override
    {
        return false;
    }
    void setAutotileUseSystemBorderColors(bool) override
    {
    }
    QStringList lockedScreens() const override
    {
        return {};
    }
    void setLockedScreens(const QStringList&) override
    {
    }
    bool isScreenLocked(const QString&) const override
    {
        return false;
    }
    void setScreenLocked(const QString&, bool) override
    {
    }
    bool isContextLocked(const QString&, int, const QString&) const override
    {
        return false;
    }
    void setContextLocked(const QString&, int, const QString&, bool) override
    {
    }

    // Persistence (ISettings)
    void load() override
    {
    }
    void save() override
    {
    }
    void reset() override
    {
    }

private:
    QString m_defaultLayoutId;
};

} // namespace PlasmaZones
