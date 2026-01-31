// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../core/interfaces.h"
#include "../core/constants.h"
#include <KConfigGroup>

namespace PlasmaZones {

/**
 * @brief Global settings for PlasmaZones
 *
 * Implements the ISettings interface with KConfig integration.
 * Supports ricer-friendly customization with color themes, opacity,
 * and integration with system color schemes.
 *
 * Note: This class does NOT use the singleton pattern. Create instances
 * where needed and pass via dependency injection.
 */
class PLASMAZONES_EXPORT Settings : public ISettings
{
    Q_OBJECT

    // Activation settings
    Q_PROPERTY(bool shiftDragToActivate READ shiftDragToActivate WRITE setShiftDragToActivate NOTIFY
                   shiftDragToActivateChanged)
    Q_PROPERTY(int dragActivationModifier READ dragActivationModifierInt WRITE setDragActivationModifierInt NOTIFY
                   dragActivationModifierChanged)
    Q_PROPERTY(
        int skipSnapModifier READ skipSnapModifierInt WRITE setSkipSnapModifierInt NOTIFY skipSnapModifierChanged)
    Q_PROPERTY(
        int multiZoneModifier READ multiZoneModifierInt WRITE setMultiZoneModifierInt NOTIFY multiZoneModifierChanged)
    Q_PROPERTY(bool middleClickMultiZone READ middleClickMultiZone WRITE setMiddleClickMultiZone NOTIFY
                   middleClickMultiZoneChanged)

    // Display settings
    Q_PROPERTY(bool showZonesOnAllMonitors READ showZonesOnAllMonitors WRITE setShowZonesOnAllMonitors NOTIFY
                   showZonesOnAllMonitorsChanged)
    Q_PROPERTY(
        QStringList disabledMonitors READ disabledMonitors WRITE setDisabledMonitors NOTIFY disabledMonitorsChanged)
    Q_PROPERTY(bool showZoneNumbers READ showZoneNumbers WRITE setShowZoneNumbers NOTIFY showZoneNumbersChanged)
    Q_PROPERTY(
        bool flashZonesOnSwitch READ flashZonesOnSwitch WRITE setFlashZonesOnSwitch NOTIFY flashZonesOnSwitchChanged)
    Q_PROPERTY(bool showOsdOnLayoutSwitch READ showOsdOnLayoutSwitch WRITE setShowOsdOnLayoutSwitch NOTIFY
                   showOsdOnLayoutSwitchChanged)
    Q_PROPERTY(bool showNavigationOsd READ showNavigationOsd WRITE setShowNavigationOsd NOTIFY
                   showNavigationOsdChanged)
    Q_PROPERTY(int osdStyle READ osdStyleInt WRITE setOsdStyleInt NOTIFY osdStyleChanged)

    // Appearance (ricer-friendly)
    Q_PROPERTY(bool useSystemColors READ useSystemColors WRITE setUseSystemColors NOTIFY useSystemColorsChanged)
    Q_PROPERTY(QColor highlightColor READ highlightColor WRITE setHighlightColor NOTIFY highlightColorChanged)
    Q_PROPERTY(QColor inactiveColor READ inactiveColor WRITE setInactiveColor NOTIFY inactiveColorChanged)
    Q_PROPERTY(QColor borderColor READ borderColor WRITE setBorderColor NOTIFY borderColorChanged)
    Q_PROPERTY(QColor numberColor READ numberColor WRITE setNumberColor NOTIFY numberColorChanged)
    Q_PROPERTY(qreal activeOpacity READ activeOpacity WRITE setActiveOpacity NOTIFY activeOpacityChanged)
    Q_PROPERTY(qreal inactiveOpacity READ inactiveOpacity WRITE setInactiveOpacity NOTIFY inactiveOpacityChanged)
    Q_PROPERTY(int borderWidth READ borderWidth WRITE setBorderWidth NOTIFY borderWidthChanged)
    Q_PROPERTY(int borderRadius READ borderRadius WRITE setBorderRadius NOTIFY borderRadiusChanged)
    Q_PROPERTY(bool enableBlur READ enableBlur WRITE setEnableBlur NOTIFY enableBlurChanged)

    // Zone settings
    Q_PROPERTY(int zonePadding READ zonePadding WRITE setZonePadding NOTIFY zonePaddingChanged)
    Q_PROPERTY(int outerGap READ outerGap WRITE setOuterGap NOTIFY outerGapChanged)
    Q_PROPERTY(int adjacentThreshold READ adjacentThreshold WRITE setAdjacentThreshold NOTIFY adjacentThresholdChanged)

    // Performance and behavior (configurable constants)
    Q_PROPERTY(int pollIntervalMs READ pollIntervalMs WRITE setPollIntervalMs NOTIFY pollIntervalMsChanged)
    Q_PROPERTY(int minimumZoneSizePx READ minimumZoneSizePx WRITE setMinimumZoneSizePx NOTIFY minimumZoneSizePxChanged)
    Q_PROPERTY(int minimumZoneDisplaySizePx READ minimumZoneDisplaySizePx WRITE setMinimumZoneDisplaySizePx NOTIFY
                   minimumZoneDisplaySizePxChanged)

    // Window behavior
    Q_PROPERTY(bool keepWindowsInZonesOnResolutionChange READ keepWindowsInZonesOnResolutionChange WRITE
                   setKeepWindowsInZonesOnResolutionChange NOTIFY keepWindowsInZonesOnResolutionChangeChanged)
    Q_PROPERTY(bool moveNewWindowsToLastZone READ moveNewWindowsToLastZone WRITE setMoveNewWindowsToLastZone NOTIFY
                   moveNewWindowsToLastZoneChanged)
    Q_PROPERTY(bool restoreOriginalSizeOnUnsnap READ restoreOriginalSizeOnUnsnap WRITE setRestoreOriginalSizeOnUnsnap
                   NOTIFY restoreOriginalSizeOnUnsnapChanged)
    Q_PROPERTY(int stickyWindowHandling READ stickyWindowHandlingInt WRITE setStickyWindowHandlingInt NOTIFY
                   stickyWindowHandlingChanged)

    // Default layout (used when no explicit assignment exists)
    Q_PROPERTY(QString defaultLayoutId READ defaultLayoutId WRITE setDefaultLayoutId NOTIFY defaultLayoutIdChanged)

    // Exclusions
    Q_PROPERTY(QStringList excludedApplications READ excludedApplications WRITE setExcludedApplications NOTIFY
                   excludedApplicationsChanged)
    Q_PROPERTY(QStringList excludedWindowClasses READ excludedWindowClasses WRITE setExcludedWindowClasses NOTIFY
                   excludedWindowClassesChanged)
    Q_PROPERTY(bool excludeTransientWindows READ excludeTransientWindows WRITE setExcludeTransientWindows NOTIFY
                   excludeTransientWindowsChanged)
    Q_PROPERTY(int minimumWindowWidth READ minimumWindowWidth WRITE setMinimumWindowWidth NOTIFY
                   minimumWindowWidthChanged)
    Q_PROPERTY(int minimumWindowHeight READ minimumWindowHeight WRITE setMinimumWindowHeight NOTIFY
                   minimumWindowHeightChanged)

    // Zone Selector
    Q_PROPERTY(bool zoneSelectorEnabled READ zoneSelectorEnabled WRITE setZoneSelectorEnabled NOTIFY
                   zoneSelectorEnabledChanged)
    Q_PROPERTY(int zoneSelectorTriggerDistance READ zoneSelectorTriggerDistance WRITE setZoneSelectorTriggerDistance
                   NOTIFY zoneSelectorTriggerDistanceChanged)
    Q_PROPERTY(int zoneSelectorPosition READ zoneSelectorPositionInt WRITE setZoneSelectorPositionInt NOTIFY
                   zoneSelectorPositionChanged)
    Q_PROPERTY(int zoneSelectorLayoutMode READ zoneSelectorLayoutModeInt WRITE setZoneSelectorLayoutModeInt NOTIFY
                   zoneSelectorLayoutModeChanged)
    Q_PROPERTY(int zoneSelectorSizeMode READ zoneSelectorSizeModeInt WRITE setZoneSelectorSizeModeInt NOTIFY
                   zoneSelectorSizeModeChanged)
    Q_PROPERTY(
        int zoneSelectorMaxRows READ zoneSelectorMaxRows WRITE setZoneSelectorMaxRows NOTIFY zoneSelectorMaxRowsChanged)
    Q_PROPERTY(int zoneSelectorPreviewWidth READ zoneSelectorPreviewWidth WRITE setZoneSelectorPreviewWidth NOTIFY
                   zoneSelectorPreviewWidthChanged)
    Q_PROPERTY(int zoneSelectorPreviewHeight READ zoneSelectorPreviewHeight WRITE setZoneSelectorPreviewHeight NOTIFY
                   zoneSelectorPreviewHeightChanged)
    Q_PROPERTY(bool zoneSelectorPreviewLockAspect READ zoneSelectorPreviewLockAspect WRITE
                   setZoneSelectorPreviewLockAspect NOTIFY zoneSelectorPreviewLockAspectChanged)
    Q_PROPERTY(int zoneSelectorGridColumns READ zoneSelectorGridColumns WRITE setZoneSelectorGridColumns NOTIFY
                   zoneSelectorGridColumnsChanged)

    // Shader Effects
    Q_PROPERTY(bool enableShaderEffects READ enableShaderEffects WRITE setEnableShaderEffects NOTIFY
                   enableShaderEffectsChanged)
    Q_PROPERTY(int shaderFrameRate READ shaderFrameRate WRITE setShaderFrameRate NOTIFY shaderFrameRateChanged)

    // Global Shortcuts (configurable via KCM, registered with KGlobalAccel)
    Q_PROPERTY(
        QString openEditorShortcut READ openEditorShortcut WRITE setOpenEditorShortcut NOTIFY openEditorShortcutChanged)
    Q_PROPERTY(QString previousLayoutShortcut READ previousLayoutShortcut WRITE setPreviousLayoutShortcut NOTIFY
                   previousLayoutShortcutChanged)
    Q_PROPERTY(
        QString nextLayoutShortcut READ nextLayoutShortcut WRITE setNextLayoutShortcut NOTIFY nextLayoutShortcutChanged)
    Q_PROPERTY(QString quickLayout1Shortcut READ quickLayout1Shortcut WRITE setQuickLayout1Shortcut NOTIFY
                   quickLayout1ShortcutChanged)
    Q_PROPERTY(QString quickLayout2Shortcut READ quickLayout2Shortcut WRITE setQuickLayout2Shortcut NOTIFY
                   quickLayout2ShortcutChanged)
    Q_PROPERTY(QString quickLayout3Shortcut READ quickLayout3Shortcut WRITE setQuickLayout3Shortcut NOTIFY
                   quickLayout3ShortcutChanged)
    Q_PROPERTY(QString quickLayout4Shortcut READ quickLayout4Shortcut WRITE setQuickLayout4Shortcut NOTIFY
                   quickLayout4ShortcutChanged)
    Q_PROPERTY(QString quickLayout5Shortcut READ quickLayout5Shortcut WRITE setQuickLayout5Shortcut NOTIFY
                   quickLayout5ShortcutChanged)
    Q_PROPERTY(QString quickLayout6Shortcut READ quickLayout6Shortcut WRITE setQuickLayout6Shortcut NOTIFY
                   quickLayout6ShortcutChanged)
    Q_PROPERTY(QString quickLayout7Shortcut READ quickLayout7Shortcut WRITE setQuickLayout7Shortcut NOTIFY
                   quickLayout7ShortcutChanged)
    Q_PROPERTY(QString quickLayout8Shortcut READ quickLayout8Shortcut WRITE setQuickLayout8Shortcut NOTIFY
                   quickLayout8ShortcutChanged)
    Q_PROPERTY(QString quickLayout9Shortcut READ quickLayout9Shortcut WRITE setQuickLayout9Shortcut NOTIFY
                   quickLayout9ShortcutChanged)

    // Keyboard Navigation Shortcuts (Phase 1 features)
    Q_PROPERTY(QString moveWindowLeftShortcut READ moveWindowLeftShortcut WRITE setMoveWindowLeftShortcut NOTIFY
                   moveWindowLeftShortcutChanged)
    Q_PROPERTY(QString moveWindowRightShortcut READ moveWindowRightShortcut WRITE setMoveWindowRightShortcut NOTIFY
                   moveWindowRightShortcutChanged)
    Q_PROPERTY(QString moveWindowUpShortcut READ moveWindowUpShortcut WRITE setMoveWindowUpShortcut NOTIFY
                   moveWindowUpShortcutChanged)
    Q_PROPERTY(QString moveWindowDownShortcut READ moveWindowDownShortcut WRITE setMoveWindowDownShortcut NOTIFY
                   moveWindowDownShortcutChanged)
    Q_PROPERTY(QString focusZoneLeftShortcut READ focusZoneLeftShortcut WRITE setFocusZoneLeftShortcut NOTIFY
                   focusZoneLeftShortcutChanged)
    Q_PROPERTY(QString focusZoneRightShortcut READ focusZoneRightShortcut WRITE setFocusZoneRightShortcut NOTIFY
                   focusZoneRightShortcutChanged)
    Q_PROPERTY(QString focusZoneUpShortcut READ focusZoneUpShortcut WRITE setFocusZoneUpShortcut NOTIFY
                   focusZoneUpShortcutChanged)
    Q_PROPERTY(QString focusZoneDownShortcut READ focusZoneDownShortcut WRITE setFocusZoneDownShortcut NOTIFY
                   focusZoneDownShortcutChanged)
    Q_PROPERTY(QString pushToEmptyZoneShortcut READ pushToEmptyZoneShortcut WRITE setPushToEmptyZoneShortcut NOTIFY
                   pushToEmptyZoneShortcutChanged)
    Q_PROPERTY(QString restoreWindowSizeShortcut READ restoreWindowSizeShortcut WRITE setRestoreWindowSizeShortcut
                   NOTIFY restoreWindowSizeShortcutChanged)
    Q_PROPERTY(QString toggleWindowFloatShortcut READ toggleWindowFloatShortcut WRITE setToggleWindowFloatShortcut
                   NOTIFY toggleWindowFloatShortcutChanged)

    // Swap Window Shortcuts (Meta+Ctrl+Alt+Arrow)
    Q_PROPERTY(QString swapWindowLeftShortcut READ swapWindowLeftShortcut WRITE setSwapWindowLeftShortcut NOTIFY
                   swapWindowLeftShortcutChanged)
    Q_PROPERTY(QString swapWindowRightShortcut READ swapWindowRightShortcut WRITE setSwapWindowRightShortcut NOTIFY
                   swapWindowRightShortcutChanged)
    Q_PROPERTY(QString swapWindowUpShortcut READ swapWindowUpShortcut WRITE setSwapWindowUpShortcut NOTIFY
                   swapWindowUpShortcutChanged)
    Q_PROPERTY(QString swapWindowDownShortcut READ swapWindowDownShortcut WRITE setSwapWindowDownShortcut NOTIFY
                   swapWindowDownShortcutChanged)

    // Snap to Zone by Number Shortcuts (Meta+Ctrl+1-9)
    Q_PROPERTY(QString snapToZone1Shortcut READ snapToZone1Shortcut WRITE setSnapToZone1Shortcut NOTIFY
                   snapToZone1ShortcutChanged)
    Q_PROPERTY(QString snapToZone2Shortcut READ snapToZone2Shortcut WRITE setSnapToZone2Shortcut NOTIFY
                   snapToZone2ShortcutChanged)
    Q_PROPERTY(QString snapToZone3Shortcut READ snapToZone3Shortcut WRITE setSnapToZone3Shortcut NOTIFY
                   snapToZone3ShortcutChanged)
    Q_PROPERTY(QString snapToZone4Shortcut READ snapToZone4Shortcut WRITE setSnapToZone4Shortcut NOTIFY
                   snapToZone4ShortcutChanged)
    Q_PROPERTY(QString snapToZone5Shortcut READ snapToZone5Shortcut WRITE setSnapToZone5Shortcut NOTIFY
                   snapToZone5ShortcutChanged)
    Q_PROPERTY(QString snapToZone6Shortcut READ snapToZone6Shortcut WRITE setSnapToZone6Shortcut NOTIFY
                   snapToZone6ShortcutChanged)
    Q_PROPERTY(QString snapToZone7Shortcut READ snapToZone7Shortcut WRITE setSnapToZone7Shortcut NOTIFY
                   snapToZone7ShortcutChanged)
    Q_PROPERTY(QString snapToZone8Shortcut READ snapToZone8Shortcut WRITE setSnapToZone8Shortcut NOTIFY
                   snapToZone8ShortcutChanged)
    Q_PROPERTY(QString snapToZone9Shortcut READ snapToZone9Shortcut WRITE setSnapToZone9Shortcut NOTIFY
                   snapToZone9ShortcutChanged)

    // Rotate Windows Shortcuts (Meta+Ctrl+[ / Meta+Ctrl+])
    // Rotates all windows in the current layout clockwise or counterclockwise
    Q_PROPERTY(QString rotateWindowsClockwiseShortcut READ rotateWindowsClockwiseShortcut WRITE
                   setRotateWindowsClockwiseShortcut NOTIFY rotateWindowsClockwiseShortcutChanged)
    Q_PROPERTY(QString rotateWindowsCounterclockwiseShortcut READ rotateWindowsCounterclockwiseShortcut WRITE
                   setRotateWindowsCounterclockwiseShortcut NOTIFY rotateWindowsCounterclockwiseShortcutChanged)

    // Cycle Windows in Zone Shortcuts (Meta+Alt+. / Meta+Alt+,)
    // Cycles focus between windows stacked in the same zone
    Q_PROPERTY(QString cycleWindowForwardShortcut READ cycleWindowForwardShortcut WRITE setCycleWindowForwardShortcut
                   NOTIFY cycleWindowForwardShortcutChanged)
    Q_PROPERTY(QString cycleWindowBackwardShortcut READ cycleWindowBackwardShortcut WRITE setCycleWindowBackwardShortcut
                   NOTIFY cycleWindowBackwardShortcutChanged)

    // ═══════════════════════════════════════════════════════════════════════════
    // Autotiling Settings (Bismuth-compatible automatic window tiling)
    // ═══════════════════════════════════════════════════════════════════════════

    Q_PROPERTY(bool autotileEnabled READ autotileEnabled WRITE setAutotileEnabled NOTIFY autotileEnabledChanged)
    Q_PROPERTY(QString autotileAlgorithm READ autotileAlgorithm WRITE setAutotileAlgorithm NOTIFY autotileAlgorithmChanged)
    Q_PROPERTY(qreal autotileSplitRatio READ autotileSplitRatio WRITE setAutotileSplitRatio NOTIFY autotileSplitRatioChanged)
    Q_PROPERTY(int autotileMasterCount READ autotileMasterCount WRITE setAutotileMasterCount NOTIFY autotileMasterCountChanged)
    Q_PROPERTY(int autotileInnerGap READ autotileInnerGap WRITE setAutotileInnerGap NOTIFY autotileInnerGapChanged)
    Q_PROPERTY(int autotileOuterGap READ autotileOuterGap WRITE setAutotileOuterGap NOTIFY autotileOuterGapChanged)
    Q_PROPERTY(bool autotileFocusNewWindows READ autotileFocusNewWindows WRITE setAutotileFocusNewWindows NOTIFY autotileFocusNewWindowsChanged)
    Q_PROPERTY(bool autotileSmartGaps READ autotileSmartGaps WRITE setAutotileSmartGaps NOTIFY autotileSmartGapsChanged)
    Q_PROPERTY(int autotileInsertPosition READ autotileInsertPositionInt WRITE setAutotileInsertPositionInt NOTIFY autotileInsertPositionChanged)

    // Autotile Animation Settings (KWin effect visual transitions)
    Q_PROPERTY(bool autotileAnimationsEnabled READ autotileAnimationsEnabled WRITE setAutotileAnimationsEnabled NOTIFY autotileAnimationsEnabledChanged)
    Q_PROPERTY(int autotileAnimationDuration READ autotileAnimationDuration WRITE setAutotileAnimationDuration NOTIFY autotileAnimationDurationChanged)

    // Additional Autotiling Settings (focus, visual feedback, monocle mode)
    Q_PROPERTY(bool autotileFocusFollowsMouse READ autotileFocusFollowsMouse WRITE setAutotileFocusFollowsMouse NOTIFY autotileFocusFollowsMouseChanged)
    Q_PROPERTY(bool autotileRespectMinimumSize READ autotileRespectMinimumSize WRITE setAutotileRespectMinimumSize NOTIFY autotileRespectMinimumSizeChanged)
    Q_PROPERTY(bool autotileShowActiveBorder READ autotileShowActiveBorder WRITE setAutotileShowActiveBorder NOTIFY autotileShowActiveBorderChanged)
    Q_PROPERTY(int autotileActiveBorderWidth READ autotileActiveBorderWidth WRITE setAutotileActiveBorderWidth NOTIFY autotileActiveBorderWidthChanged)
    Q_PROPERTY(bool autotileUseSystemBorderColor READ autotileUseSystemBorderColor WRITE setAutotileUseSystemBorderColor NOTIFY autotileUseSystemBorderColorChanged)
    Q_PROPERTY(QColor autotileActiveBorderColor READ autotileActiveBorderColor WRITE setAutotileActiveBorderColor NOTIFY autotileActiveBorderColorChanged)
    Q_PROPERTY(bool autotileMonocleHideOthers READ autotileMonocleHideOthers WRITE setAutotileMonocleHideOthers NOTIFY autotileMonocleHideOthersChanged)
    Q_PROPERTY(bool autotileMonocleShowTabs READ autotileMonocleShowTabs WRITE setAutotileMonocleShowTabs NOTIFY autotileMonocleShowTabsChanged)

    // Autotiling Keyboard Shortcuts (Bismuth-compatible)
    Q_PROPERTY(QString autotileToggleShortcut READ autotileToggleShortcut WRITE setAutotileToggleShortcut NOTIFY autotileToggleShortcutChanged)
    Q_PROPERTY(QString autotileFocusMasterShortcut READ autotileFocusMasterShortcut WRITE setAutotileFocusMasterShortcut NOTIFY autotileFocusMasterShortcutChanged)
    Q_PROPERTY(QString autotileSwapMasterShortcut READ autotileSwapMasterShortcut WRITE setAutotileSwapMasterShortcut NOTIFY autotileSwapMasterShortcutChanged)
    Q_PROPERTY(QString autotileIncMasterRatioShortcut READ autotileIncMasterRatioShortcut WRITE setAutotileIncMasterRatioShortcut NOTIFY autotileIncMasterRatioShortcutChanged)
    Q_PROPERTY(QString autotileDecMasterRatioShortcut READ autotileDecMasterRatioShortcut WRITE setAutotileDecMasterRatioShortcut NOTIFY autotileDecMasterRatioShortcutChanged)
    Q_PROPERTY(QString autotileIncMasterCountShortcut READ autotileIncMasterCountShortcut WRITE setAutotileIncMasterCountShortcut NOTIFY autotileIncMasterCountShortcutChanged)
    Q_PROPERTY(QString autotileDecMasterCountShortcut READ autotileDecMasterCountShortcut WRITE setAutotileDecMasterCountShortcut NOTIFY autotileDecMasterCountShortcutChanged)
    Q_PROPERTY(QString autotileRetileShortcut READ autotileRetileShortcut WRITE setAutotileRetileShortcut NOTIFY autotileRetileShortcutChanged)

public:
    explicit Settings(QObject* parent = nullptr);
    ~Settings() override = default;

    // No singleton - use dependency injection instead

    // ISettings interface implementation
    bool shiftDragToActivate() const override
    {
        return m_shiftDragToActivate;
    }
    void setShiftDragToActivate(bool enable) override;

    DragModifier dragActivationModifier() const override
    {
        return m_dragActivationModifier;
    }
    void setDragActivationModifier(DragModifier modifier) override;
    int dragActivationModifierInt() const
    {
        return static_cast<int>(m_dragActivationModifier);
    }
    void setDragActivationModifierInt(int modifier);

    DragModifier skipSnapModifier() const override
    {
        return m_skipSnapModifier;
    }
    void setSkipSnapModifier(DragModifier modifier) override;
    int skipSnapModifierInt() const
    {
        return static_cast<int>(m_skipSnapModifier);
    }
    void setSkipSnapModifierInt(int modifier);
    DragModifier multiZoneModifier() const override
    {
        return m_multiZoneModifier;
    }
    void setMultiZoneModifier(DragModifier modifier) override;
    int multiZoneModifierInt() const
    {
        return static_cast<int>(m_multiZoneModifier);
    }
    void setMultiZoneModifierInt(int modifier);

    bool middleClickMultiZone() const override
    {
        return m_middleClickMultiZone;
    }
    void setMiddleClickMultiZone(bool enable) override;

    bool showZonesOnAllMonitors() const override
    {
        return m_showZonesOnAllMonitors;
    }
    void setShowZonesOnAllMonitors(bool show) override;

    QStringList disabledMonitors() const override
    {
        return m_disabledMonitors;
    }
    void setDisabledMonitors(const QStringList& screenNames) override;
    bool isMonitorDisabled(const QString& screenName) const override;

    bool showZoneNumbers() const override
    {
        return m_showZoneNumbers;
    }
    void setShowZoneNumbers(bool show) override;

    bool flashZonesOnSwitch() const override
    {
        return m_flashZonesOnSwitch;
    }
    void setFlashZonesOnSwitch(bool flash) override;

    bool showOsdOnLayoutSwitch() const override
    {
        return m_showOsdOnLayoutSwitch;
    }
    void setShowOsdOnLayoutSwitch(bool show) override;

    bool showNavigationOsd() const override
    {
        return m_showNavigationOsd;
    }
    void setShowNavigationOsd(bool show) override;

    OsdStyle osdStyle() const override
    {
        return m_osdStyle;
    }
    void setOsdStyle(OsdStyle style) override;
    int osdStyleInt() const
    {
        return static_cast<int>(m_osdStyle);
    }
    void setOsdStyleInt(int style);

    bool useSystemColors() const override
    {
        return m_useSystemColors;
    }
    void setUseSystemColors(bool use) override;

    QColor highlightColor() const override
    {
        return m_highlightColor;
    }
    void setHighlightColor(const QColor& color) override;

    QColor inactiveColor() const override
    {
        return m_inactiveColor;
    }
    void setInactiveColor(const QColor& color) override;

    QColor borderColor() const override
    {
        return m_borderColor;
    }
    void setBorderColor(const QColor& color) override;

    QColor numberColor() const override
    {
        return m_numberColor;
    }
    void setNumberColor(const QColor& color) override;

    qreal activeOpacity() const override
    {
        return m_activeOpacity;
    }
    void setActiveOpacity(qreal opacity) override;

    qreal inactiveOpacity() const override
    {
        return m_inactiveOpacity;
    }
    void setInactiveOpacity(qreal opacity) override;

    int borderWidth() const override
    {
        return m_borderWidth;
    }
    void setBorderWidth(int width) override;

    int borderRadius() const override
    {
        return m_borderRadius;
    }
    void setBorderRadius(int radius) override;

    bool enableBlur() const override
    {
        return m_enableBlur;
    }
    void setEnableBlur(bool enable) override;

    int zonePadding() const override
    {
        return m_zonePadding;
    }
    void setZonePadding(int padding) override;

    int outerGap() const override
    {
        return m_outerGap;
    }
    void setOuterGap(int gap) override;

    int adjacentThreshold() const override
    {
        return m_adjacentThreshold;
    }
    void setAdjacentThreshold(int threshold) override;

    int pollIntervalMs() const override
    {
        return m_pollIntervalMs;
    }
    void setPollIntervalMs(int interval) override;

    int minimumZoneSizePx() const override
    {
        return m_minimumZoneSizePx;
    }
    void setMinimumZoneSizePx(int size) override;

    int minimumZoneDisplaySizePx() const override
    {
        return m_minimumZoneDisplaySizePx;
    }
    void setMinimumZoneDisplaySizePx(int size) override;

    bool keepWindowsInZonesOnResolutionChange() const override
    {
        return m_keepWindowsInZonesOnResolutionChange;
    }
    void setKeepWindowsInZonesOnResolutionChange(bool keep) override;

    bool moveNewWindowsToLastZone() const override
    {
        return m_moveNewWindowsToLastZone;
    }
    void setMoveNewWindowsToLastZone(bool move) override;

    bool restoreOriginalSizeOnUnsnap() const override
    {
        return m_restoreOriginalSizeOnUnsnap;
    }
    void setRestoreOriginalSizeOnUnsnap(bool restore) override;
    StickyWindowHandling stickyWindowHandling() const override
    {
        return m_stickyWindowHandling;
    }
    void setStickyWindowHandling(StickyWindowHandling handling) override;
    int stickyWindowHandlingInt() const
    {
        return static_cast<int>(m_stickyWindowHandling);
    }
    void setStickyWindowHandlingInt(int handling);

    QString defaultLayoutId() const override
    {
        return m_defaultLayoutId;
    }
    void setDefaultLayoutId(const QString& layoutId) override;

    QStringList excludedApplications() const override
    {
        return m_excludedApplications;
    }
    void setExcludedApplications(const QStringList& apps) override;

    QStringList excludedWindowClasses() const override
    {
        return m_excludedWindowClasses;
    }
    void setExcludedWindowClasses(const QStringList& classes) override;

    bool excludeTransientWindows() const override
    {
        return m_excludeTransientWindows;
    }
    void setExcludeTransientWindows(bool exclude) override;

    int minimumWindowWidth() const override
    {
        return m_minimumWindowWidth;
    }
    void setMinimumWindowWidth(int width) override;

    int minimumWindowHeight() const override
    {
        return m_minimumWindowHeight;
    }
    void setMinimumWindowHeight(int height) override;

    // Zone Selector
    bool zoneSelectorEnabled() const override
    {
        return m_zoneSelectorEnabled;
    }
    void setZoneSelectorEnabled(bool enabled) override;
    int zoneSelectorTriggerDistance() const override
    {
        return m_zoneSelectorTriggerDistance;
    }
    void setZoneSelectorTriggerDistance(int distance) override;
    ZoneSelectorPosition zoneSelectorPosition() const override
    {
        return m_zoneSelectorPosition;
    }
    void setZoneSelectorPosition(ZoneSelectorPosition position) override;
    int zoneSelectorPositionInt() const
    {
        return static_cast<int>(m_zoneSelectorPosition);
    }
    void setZoneSelectorPositionInt(int position);
    ZoneSelectorLayoutMode zoneSelectorLayoutMode() const override
    {
        return m_zoneSelectorLayoutMode;
    }
    void setZoneSelectorLayoutMode(ZoneSelectorLayoutMode mode) override;
    int zoneSelectorLayoutModeInt() const
    {
        return static_cast<int>(m_zoneSelectorLayoutMode);
    }
    void setZoneSelectorLayoutModeInt(int mode);
    int zoneSelectorPreviewWidth() const override
    {
        return m_zoneSelectorPreviewWidth;
    }
    void setZoneSelectorPreviewWidth(int width) override;
    int zoneSelectorPreviewHeight() const override
    {
        return m_zoneSelectorPreviewHeight;
    }
    void setZoneSelectorPreviewHeight(int height) override;
    bool zoneSelectorPreviewLockAspect() const override
    {
        return m_zoneSelectorPreviewLockAspect;
    }
    void setZoneSelectorPreviewLockAspect(bool locked) override;
    int zoneSelectorGridColumns() const override
    {
        return m_zoneSelectorGridColumns;
    }
    void setZoneSelectorGridColumns(int columns) override;
    ZoneSelectorSizeMode zoneSelectorSizeMode() const override
    {
        return m_zoneSelectorSizeMode;
    }
    void setZoneSelectorSizeMode(ZoneSelectorSizeMode mode) override;
    int zoneSelectorSizeModeInt() const
    {
        return static_cast<int>(m_zoneSelectorSizeMode);
    }
    void setZoneSelectorSizeModeInt(int mode);
    int zoneSelectorMaxRows() const override
    {
        return m_zoneSelectorMaxRows;
    }
    void setZoneSelectorMaxRows(int rows) override;

    // Shader Effects
    bool enableShaderEffects() const override
    {
        return m_enableShaderEffects;
    }
    void setEnableShaderEffects(bool enable) override;
    int shaderFrameRate() const override
    {
        return m_shaderFrameRate;
    }
    void setShaderFrameRate(int fps) override;

    // Global Shortcuts (for KGlobalAccel)
    QString openEditorShortcut() const
    {
        return m_openEditorShortcut;
    }
    void setOpenEditorShortcut(const QString& shortcut);
    QString previousLayoutShortcut() const
    {
        return m_previousLayoutShortcut;
    }
    void setPreviousLayoutShortcut(const QString& shortcut);
    QString nextLayoutShortcut() const
    {
        return m_nextLayoutShortcut;
    }
    void setNextLayoutShortcut(const QString& shortcut);
    QString quickLayout1Shortcut() const
    {
        return m_quickLayoutShortcuts[0];
    }
    void setQuickLayout1Shortcut(const QString& shortcut);
    QString quickLayout2Shortcut() const
    {
        return m_quickLayoutShortcuts[1];
    }
    void setQuickLayout2Shortcut(const QString& shortcut);
    QString quickLayout3Shortcut() const
    {
        return m_quickLayoutShortcuts[2];
    }
    void setQuickLayout3Shortcut(const QString& shortcut);
    QString quickLayout4Shortcut() const
    {
        return m_quickLayoutShortcuts[3];
    }
    void setQuickLayout4Shortcut(const QString& shortcut);
    QString quickLayout5Shortcut() const
    {
        return m_quickLayoutShortcuts[4];
    }
    void setQuickLayout5Shortcut(const QString& shortcut);
    QString quickLayout6Shortcut() const
    {
        return m_quickLayoutShortcuts[5];
    }
    void setQuickLayout6Shortcut(const QString& shortcut);
    QString quickLayout7Shortcut() const
    {
        return m_quickLayoutShortcuts[6];
    }
    void setQuickLayout7Shortcut(const QString& shortcut);
    QString quickLayout8Shortcut() const
    {
        return m_quickLayoutShortcuts[7];
    }
    void setQuickLayout8Shortcut(const QString& shortcut);
    QString quickLayout9Shortcut() const
    {
        return m_quickLayoutShortcuts[8];
    }
    void setQuickLayout9Shortcut(const QString& shortcut);

    // Helper to get quick layout shortcut by index (0-8)
    QString quickLayoutShortcut(int index) const;
    void setQuickLayoutShortcut(int index, const QString& shortcut);

    // Keyboard Navigation Shortcuts (Phase 1 features)
    QString moveWindowLeftShortcut() const
    {
        return m_moveWindowLeftShortcut;
    }
    void setMoveWindowLeftShortcut(const QString& shortcut);
    QString moveWindowRightShortcut() const
    {
        return m_moveWindowRightShortcut;
    }
    void setMoveWindowRightShortcut(const QString& shortcut);
    QString moveWindowUpShortcut() const
    {
        return m_moveWindowUpShortcut;
    }
    void setMoveWindowUpShortcut(const QString& shortcut);
    QString moveWindowDownShortcut() const
    {
        return m_moveWindowDownShortcut;
    }
    void setMoveWindowDownShortcut(const QString& shortcut);
    QString focusZoneLeftShortcut() const
    {
        return m_focusZoneLeftShortcut;
    }
    void setFocusZoneLeftShortcut(const QString& shortcut);
    QString focusZoneRightShortcut() const
    {
        return m_focusZoneRightShortcut;
    }
    void setFocusZoneRightShortcut(const QString& shortcut);
    QString focusZoneUpShortcut() const
    {
        return m_focusZoneUpShortcut;
    }
    void setFocusZoneUpShortcut(const QString& shortcut);
    QString focusZoneDownShortcut() const
    {
        return m_focusZoneDownShortcut;
    }
    void setFocusZoneDownShortcut(const QString& shortcut);
    QString pushToEmptyZoneShortcut() const
    {
        return m_pushToEmptyZoneShortcut;
    }
    void setPushToEmptyZoneShortcut(const QString& shortcut);
    QString restoreWindowSizeShortcut() const
    {
        return m_restoreWindowSizeShortcut;
    }
    void setRestoreWindowSizeShortcut(const QString& shortcut);
    QString toggleWindowFloatShortcut() const
    {
        return m_toggleWindowFloatShortcut;
    }
    void setToggleWindowFloatShortcut(const QString& shortcut);

    // Swap Window Shortcuts (Meta+Ctrl+Alt+Arrow)
    QString swapWindowLeftShortcut() const
    {
        return m_swapWindowLeftShortcut;
    }
    void setSwapWindowLeftShortcut(const QString& shortcut);
    QString swapWindowRightShortcut() const
    {
        return m_swapWindowRightShortcut;
    }
    void setSwapWindowRightShortcut(const QString& shortcut);
    QString swapWindowUpShortcut() const
    {
        return m_swapWindowUpShortcut;
    }
    void setSwapWindowUpShortcut(const QString& shortcut);
    QString swapWindowDownShortcut() const
    {
        return m_swapWindowDownShortcut;
    }
    void setSwapWindowDownShortcut(const QString& shortcut);

    // Snap to Zone by Number Shortcuts (Meta+Ctrl+1-9)
    QString snapToZone1Shortcut() const
    {
        return m_snapToZoneShortcuts[0];
    }
    void setSnapToZone1Shortcut(const QString& shortcut);
    QString snapToZone2Shortcut() const
    {
        return m_snapToZoneShortcuts[1];
    }
    void setSnapToZone2Shortcut(const QString& shortcut);
    QString snapToZone3Shortcut() const
    {
        return m_snapToZoneShortcuts[2];
    }
    void setSnapToZone3Shortcut(const QString& shortcut);
    QString snapToZone4Shortcut() const
    {
        return m_snapToZoneShortcuts[3];
    }
    void setSnapToZone4Shortcut(const QString& shortcut);
    QString snapToZone5Shortcut() const
    {
        return m_snapToZoneShortcuts[4];
    }
    void setSnapToZone5Shortcut(const QString& shortcut);
    QString snapToZone6Shortcut() const
    {
        return m_snapToZoneShortcuts[5];
    }
    void setSnapToZone6Shortcut(const QString& shortcut);
    QString snapToZone7Shortcut() const
    {
        return m_snapToZoneShortcuts[6];
    }
    void setSnapToZone7Shortcut(const QString& shortcut);
    QString snapToZone8Shortcut() const
    {
        return m_snapToZoneShortcuts[7];
    }
    void setSnapToZone8Shortcut(const QString& shortcut);
    QString snapToZone9Shortcut() const
    {
        return m_snapToZoneShortcuts[8];
    }
    void setSnapToZone9Shortcut(const QString& shortcut);

    // Helper to get snap-to-zone shortcut by index (0-8)
    QString snapToZoneShortcut(int index) const;
    void setSnapToZoneShortcut(int index, const QString& shortcut);

    // Rotate Windows Shortcuts (Meta+Ctrl+[ / Meta+Ctrl+])
    QString rotateWindowsClockwiseShortcut() const
    {
        return m_rotateWindowsClockwiseShortcut;
    }
    void setRotateWindowsClockwiseShortcut(const QString& shortcut);
    QString rotateWindowsCounterclockwiseShortcut() const
    {
        return m_rotateWindowsCounterclockwiseShortcut;
    }
    void setRotateWindowsCounterclockwiseShortcut(const QString& shortcut);

    // Cycle Windows in Zone Shortcuts (Meta+Alt+. / Meta+Alt+,)
    QString cycleWindowForwardShortcut() const
    {
        return m_cycleWindowForwardShortcut;
    }
    void setCycleWindowForwardShortcut(const QString& shortcut);
    QString cycleWindowBackwardShortcut() const
    {
        return m_cycleWindowBackwardShortcut;
    }
    void setCycleWindowBackwardShortcut(const QString& shortcut);

    // ═══════════════════════════════════════════════════════════════════════════
    // Autotiling Settings (IAutotileSettings interface)
    // ═══════════════════════════════════════════════════════════════════════════

    bool autotileEnabled() const override { return m_autotileEnabled; }
    void setAutotileEnabled(bool enabled) override;

    QString autotileAlgorithm() const override { return m_autotileAlgorithm; }
    void setAutotileAlgorithm(const QString& algorithm) override;

    qreal autotileSplitRatio() const override { return m_autotileSplitRatio; }
    void setAutotileSplitRatio(qreal ratio) override;

    int autotileMasterCount() const override { return m_autotileMasterCount; }
    void setAutotileMasterCount(int count) override;

    int autotileInnerGap() const override { return m_autotileInnerGap; }
    void setAutotileInnerGap(int gap) override;

    int autotileOuterGap() const override { return m_autotileOuterGap; }
    void setAutotileOuterGap(int gap) override;

    bool autotileFocusNewWindows() const override { return m_autotileFocusNewWindows; }
    void setAutotileFocusNewWindows(bool focus) override;

    bool autotileSmartGaps() const override { return m_autotileSmartGaps; }
    void setAutotileSmartGaps(bool smart) override;

    enum class AutotileInsertPosition { End = 0, AfterFocused = 1, AsMaster = 2 };
    AutotileInsertPosition autotileInsertPosition() const { return m_autotileInsertPosition; }
    void setAutotileInsertPosition(AutotileInsertPosition position);
    int autotileInsertPositionInt() const { return static_cast<int>(m_autotileInsertPosition); }
    void setAutotileInsertPositionInt(int position);

    // Autotiling Shortcuts
    QString autotileToggleShortcut() const { return m_autotileToggleShortcut; }
    void setAutotileToggleShortcut(const QString& shortcut);

    QString autotileFocusMasterShortcut() const { return m_autotileFocusMasterShortcut; }
    void setAutotileFocusMasterShortcut(const QString& shortcut);

    QString autotileSwapMasterShortcut() const { return m_autotileSwapMasterShortcut; }
    void setAutotileSwapMasterShortcut(const QString& shortcut);

    QString autotileIncMasterRatioShortcut() const { return m_autotileIncMasterRatioShortcut; }
    void setAutotileIncMasterRatioShortcut(const QString& shortcut);

    QString autotileDecMasterRatioShortcut() const { return m_autotileDecMasterRatioShortcut; }
    void setAutotileDecMasterRatioShortcut(const QString& shortcut);

    QString autotileIncMasterCountShortcut() const { return m_autotileIncMasterCountShortcut; }
    void setAutotileIncMasterCountShortcut(const QString& shortcut);

    QString autotileDecMasterCountShortcut() const { return m_autotileDecMasterCountShortcut; }
    void setAutotileDecMasterCountShortcut(const QString& shortcut);

    QString autotileRetileShortcut() const { return m_autotileRetileShortcut; }
    void setAutotileRetileShortcut(const QString& shortcut);

    // Autotile Animation Settings (KWin effect visual transitions)
    bool autotileAnimationsEnabled() const override { return m_autotileAnimationsEnabled; }
    void setAutotileAnimationsEnabled(bool enabled) override;

    int autotileAnimationDuration() const override { return m_autotileAnimationDuration; }
    void setAutotileAnimationDuration(int duration) override;

    // Additional Autotiling Settings
    bool autotileFocusFollowsMouse() const override { return m_autotileFocusFollowsMouse; }
    void setAutotileFocusFollowsMouse(bool focus) override;

    bool autotileRespectMinimumSize() const override { return m_autotileRespectMinimumSize; }
    void setAutotileRespectMinimumSize(bool respect) override;

    bool autotileShowActiveBorder() const override { return m_autotileShowActiveBorder; }
    void setAutotileShowActiveBorder(bool show) override;

    int autotileActiveBorderWidth() const override { return m_autotileActiveBorderWidth; }
    void setAutotileActiveBorderWidth(int width) override;

    bool autotileUseSystemBorderColor() const override { return m_autotileUseSystemBorderColor; }
    void setAutotileUseSystemBorderColor(bool use) override;

    QColor autotileActiveBorderColor() const override { return m_autotileActiveBorderColor; }
    void setAutotileActiveBorderColor(const QColor& color) override;

    bool autotileMonocleHideOthers() const override { return m_autotileMonocleHideOthers; }
    void setAutotileMonocleHideOthers(bool hide) override;

    bool autotileMonocleShowTabs() const override { return m_autotileMonocleShowTabs; }
    void setAutotileMonocleShowTabs(bool show) override;

    // Persistence
    void load() override;
    void save() override;
    void reset() override;

    // Additional methods
    Q_INVOKABLE bool isWindowExcluded(const QString& appName, const QString& windowClass) const override;
    Q_INVOKABLE void loadColorsFromFile(const QString& filePath);
    Q_INVOKABLE void applySystemColorScheme();

private:
    // Activation
    bool m_shiftDragToActivate = true; // Deprecated - kept for migration
    // KWin Effect provides modifiers via mouseChanged signal
    // Default: Alt+Drag to show zones (matches reset() function and common user expectation)
    DragModifier m_dragActivationModifier = DragModifier::Alt; // Default: Alt for zone activation
    DragModifier m_skipSnapModifier = DragModifier::Shift; // Default: Shift to skip snap
    DragModifier m_multiZoneModifier = DragModifier::CtrlAlt; // Default: Ctrl+Alt for multi-zone
    bool m_middleClickMultiZone = true;

    // Display
    bool m_showZonesOnAllMonitors = false;
    QStringList m_disabledMonitors;
    bool m_showZoneNumbers = true;
    bool m_flashZonesOnSwitch = true;
    bool m_showOsdOnLayoutSwitch = true;
    bool m_showNavigationOsd = true;
    OsdStyle m_osdStyle = OsdStyle::Preview; // Default to visual preview

    // Appearance (DRY: use constants from Defaults namespace)
    bool m_useSystemColors = true;
    QColor m_highlightColor = Defaults::HighlightColor;
    QColor m_inactiveColor = Defaults::InactiveColor;
    QColor m_borderColor = Defaults::BorderColor;
    QColor m_numberColor = Defaults::NumberColor;
    qreal m_activeOpacity = Defaults::Opacity;
    qreal m_inactiveOpacity = Defaults::InactiveOpacity;
    int m_borderWidth = Defaults::BorderWidth;
    int m_borderRadius = Defaults::BorderRadius;
    bool m_enableBlur = true;

    // Zone settings (DRY: use constants from Defaults namespace)
    int m_zonePadding = Defaults::ZonePadding;
    int m_outerGap = Defaults::OuterGap;
    int m_adjacentThreshold = Defaults::AdjacentThreshold;

    // Performance and behavior (DRY: use constants from Defaults namespace)
    int m_pollIntervalMs = Defaults::PollIntervalMs;
    int m_minimumZoneSizePx = Defaults::MinimumZoneSizePx;
    int m_minimumZoneDisplaySizePx = Defaults::MinimumZoneDisplaySizePx;

    // Window behavior
    bool m_keepWindowsInZonesOnResolutionChange = true;
    bool m_moveNewWindowsToLastZone = false;
    bool m_restoreOriginalSizeOnUnsnap = true;
    StickyWindowHandling m_stickyWindowHandling = StickyWindowHandling::TreatAsNormal;

    // Default layout (used when no explicit assignment exists)
    QString m_defaultLayoutId;

    // Exclusions
    QStringList m_excludedApplications;
    QStringList m_excludedWindowClasses;
    bool m_excludeTransientWindows = true;
    int m_minimumWindowWidth = 200;
    int m_minimumWindowHeight = 150;

    // Zone Selector
    // Note: These member initializers are fallbacks only. Actual defaults come from
    // plasmazones.kcfg via ConfigDefaults. See reset() and load() methods.
    // If you change a default here, also update plasmazones.kcfg to match.
    bool m_zoneSelectorEnabled = true;
    int m_zoneSelectorTriggerDistance = 50; // pixels from edge to trigger
    ZoneSelectorPosition m_zoneSelectorPosition = ZoneSelectorPosition::Top;
    ZoneSelectorLayoutMode m_zoneSelectorLayoutMode = ZoneSelectorLayoutMode::Grid;
    ZoneSelectorSizeMode m_zoneSelectorSizeMode = ZoneSelectorSizeMode::Auto;
    int m_zoneSelectorMaxRows = 4; // max visible rows before scrolling
    int m_zoneSelectorPreviewWidth = 180; // preview width in pixels (Manual mode)
    int m_zoneSelectorPreviewHeight = 101; // preview height in pixels (Manual mode, when unlocked)
    bool m_zoneSelectorPreviewLockAspect = true;
    int m_zoneSelectorGridColumns = 5; // grid columns (Manual mode)

    // Shader Effects (defaults from plasmazones.kcfg via ConfigDefaults)
    bool m_enableShaderEffects = true;
    int m_shaderFrameRate = 60;

    // Global Shortcuts (configurable, registered with KGlobalAccel)
    QString m_openEditorShortcut = QStringLiteral("Meta+Shift+E");
    QString m_previousLayoutShortcut = QStringLiteral("Meta+Alt+[");
    QString m_nextLayoutShortcut = QStringLiteral("Meta+Alt+]");
    QString m_quickLayoutShortcuts[9] = {
        QStringLiteral("Meta+Alt+1"), QStringLiteral("Meta+Alt+2"), QStringLiteral("Meta+Alt+3"),
        QStringLiteral("Meta+Alt+4"), QStringLiteral("Meta+Alt+5"), QStringLiteral("Meta+Alt+6"),
        QStringLiteral("Meta+Alt+7"), QStringLiteral("Meta+Alt+8"), QStringLiteral("Meta+Alt+9")};

    // Keyboard Navigation Shortcuts (Phase 1 features)
    // Meta+Shift+Left/Right conflicts with KDE's "Window to Next/Previous Screen";
    // we use Meta+Alt+Shift+Arrow instead.
    QString m_moveWindowLeftShortcut = QStringLiteral("Meta+Alt+Shift+Left");
    QString m_moveWindowRightShortcut = QStringLiteral("Meta+Alt+Shift+Right");
    QString m_moveWindowUpShortcut = QStringLiteral("Meta+Alt+Shift+Up");
    QString m_moveWindowDownShortcut = QStringLiteral("Meta+Alt+Shift+Down");
    // Meta+Arrow conflicts with KDE's Quick Tile; we use Alt+Shift+Arrow instead.
    QString m_focusZoneLeftShortcut = QStringLiteral("Alt+Shift+Left");
    QString m_focusZoneRightShortcut = QStringLiteral("Alt+Shift+Right");
    QString m_focusZoneUpShortcut = QStringLiteral("Alt+Shift+Up");
    QString m_focusZoneDownShortcut = QStringLiteral("Alt+Shift+Down");
    QString m_pushToEmptyZoneShortcut = QStringLiteral("Meta+Alt+Return");
    QString m_restoreWindowSizeShortcut = QStringLiteral("Meta+Alt+Escape");
    QString m_toggleWindowFloatShortcut = QStringLiteral("Meta+Alt+F");

    // Swap Window Shortcuts (Meta+Ctrl+Alt+Arrow)
    // Swaps focused window with window in adjacent zone
    // Meta+Ctrl+Arrow conflicts with KDE's virtual desktop switching;
    // we add Alt to make Meta+Ctrl+Alt+Arrow for swap operations.
    QString m_swapWindowLeftShortcut = QStringLiteral("Meta+Ctrl+Alt+Left");
    QString m_swapWindowRightShortcut = QStringLiteral("Meta+Ctrl+Alt+Right");
    QString m_swapWindowUpShortcut = QStringLiteral("Meta+Ctrl+Alt+Up");
    QString m_swapWindowDownShortcut = QStringLiteral("Meta+Ctrl+Alt+Down");

    // Snap to Zone by Number Shortcuts (Meta+Ctrl+1-9)
    // Meta+1-9 conflicts with KDE's virtual desktop switching; we use Meta+Ctrl+1-9 instead.
    QString m_snapToZoneShortcuts[9] = {
        QStringLiteral("Meta+Ctrl+1"), QStringLiteral("Meta+Ctrl+2"), QStringLiteral("Meta+Ctrl+3"),
        QStringLiteral("Meta+Ctrl+4"), QStringLiteral("Meta+Ctrl+5"), QStringLiteral("Meta+Ctrl+6"),
        QStringLiteral("Meta+Ctrl+7"), QStringLiteral("Meta+Ctrl+8"), QStringLiteral("Meta+Ctrl+9")};

    // Rotate Windows Shortcuts (Meta+Ctrl+[ / Meta+Ctrl+])
    // Rotates all windows in the current layout clockwise or counterclockwise
    QString m_rotateWindowsClockwiseShortcut = QStringLiteral("Meta+Ctrl+]");
    QString m_rotateWindowsCounterclockwiseShortcut = QStringLiteral("Meta+Ctrl+[");

    // Cycle Windows in Zone Shortcuts (Meta+Alt+. / Meta+Alt+,)
    // Cycles focus between windows stacked in the same zone (monocle-style navigation)
    QString m_cycleWindowForwardShortcut = QStringLiteral("Meta+Alt+.");
    QString m_cycleWindowBackwardShortcut = QStringLiteral("Meta+Alt+,");

    // ═══════════════════════════════════════════════════════════════════════════
    // Autotiling Settings (Bismuth-compatible)
    // ═══════════════════════════════════════════════════════════════════════════

    bool m_autotileEnabled = false;
    QString m_autotileAlgorithm = QStringLiteral("master-stack");
    qreal m_autotileSplitRatio = 0.6;
    int m_autotileMasterCount = 1;
    int m_autotileInnerGap = 8;
    int m_autotileOuterGap = 8;
    bool m_autotileFocusNewWindows = true;
    bool m_autotileSmartGaps = true;
    AutotileInsertPosition m_autotileInsertPosition = AutotileInsertPosition::End;

    // Autotile Animation Settings (KWin effect visual transitions)
    bool m_autotileAnimationsEnabled = true;
    int m_autotileAnimationDuration = 150; // milliseconds

    // Additional Autotiling Settings
    bool m_autotileFocusFollowsMouse = false;
    bool m_autotileRespectMinimumSize = false;
    bool m_autotileShowActiveBorder = true;
    int m_autotileActiveBorderWidth = 2;
    bool m_autotileUseSystemBorderColor = true;
    QColor m_autotileActiveBorderColor; // Initialized from KColorScheme in load()/reset()
    bool m_autotileMonocleHideOthers = false;
    bool m_autotileMonocleShowTabs = true;

    // Autotiling Keyboard Shortcuts (Bismuth-compatible defaults)
    QString m_autotileToggleShortcut = QStringLiteral("Meta+T");
    QString m_autotileFocusMasterShortcut = QStringLiteral("Meta+M");
    QString m_autotileSwapMasterShortcut = QStringLiteral("Meta+Return");
    QString m_autotileIncMasterRatioShortcut = QStringLiteral("Meta+Shift+=");
    QString m_autotileDecMasterRatioShortcut = QStringLiteral("Meta+Shift+-");
    QString m_autotileIncMasterCountShortcut = QStringLiteral("Meta+Shift+I");
    QString m_autotileDecMasterCountShortcut = QStringLiteral("Meta+Shift+D");
    QString m_autotileRetileShortcut = QStringLiteral("Meta+Shift+R");
};

} // namespace PlasmaZones
