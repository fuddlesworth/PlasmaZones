// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <KQuickConfigModule>
#include <KConfigGroup>
#include <QObject>
#include <QColor>
#include <QMap>
#include <QSet>
#include <QDBusMessage>
#include <functional>

class QTimer;
class QProcess;

namespace PlasmaZones {

/**
 * @brief KCM-specific constants
 */
namespace KCMConstants {
constexpr int DaemonStatusPollIntervalMs = 2000;
constexpr const char* SystemdServiceName = "plasmazones.service";
}

class Settings;

/**
 * @brief KDE Control Module for PlasmaZones settings
 *
 * Pages:
 * - Layouts: View, create, edit, import/export layouts
 * - Appearance: Color and style customization
 * - Behavior: Window snapping options
 * - Exclusions: Apps and windows to exclude from snapping
 */
class KCMPlasmaZones : public KQuickConfigModule
{
    Q_OBJECT

    // Activation
    Q_PROPERTY(bool shiftDragToActivate READ shiftDragToActivate WRITE setShiftDragToActivate NOTIFY
                   shiftDragToActivateChanged)
    Q_PROPERTY(int dragActivationModifier READ dragActivationModifier WRITE setDragActivationModifier NOTIFY
                   dragActivationModifierChanged)
    Q_PROPERTY(int multiZoneModifier READ multiZoneModifier WRITE setMultiZoneModifier NOTIFY multiZoneModifierChanged)
    Q_PROPERTY(bool middleClickMultiZone READ middleClickMultiZone WRITE setMiddleClickMultiZone NOTIFY
                   middleClickMultiZoneChanged)

    // Display
    Q_PROPERTY(bool showZonesOnAllMonitors READ showZonesOnAllMonitors WRITE setShowZonesOnAllMonitors NOTIFY
                   showZonesOnAllMonitorsChanged)
    Q_PROPERTY(QStringList disabledMonitors READ disabledMonitors NOTIFY disabledMonitorsChanged)
    Q_PROPERTY(bool showZoneNumbers READ showZoneNumbers WRITE setShowZoneNumbers NOTIFY showZoneNumbersChanged)
    Q_PROPERTY(
        bool flashZonesOnSwitch READ flashZonesOnSwitch WRITE setFlashZonesOnSwitch NOTIFY flashZonesOnSwitchChanged)
    Q_PROPERTY(bool showOsdOnLayoutSwitch READ showOsdOnLayoutSwitch WRITE setShowOsdOnLayoutSwitch NOTIFY
                   showOsdOnLayoutSwitchChanged)
    Q_PROPERTY(bool showNavigationOsd READ showNavigationOsd WRITE setShowNavigationOsd NOTIFY
                   showNavigationOsdChanged)
    Q_PROPERTY(int osdStyle READ osdStyle WRITE setOsdStyle NOTIFY osdStyleChanged)

    // Appearance
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

    // Shader Effects
    Q_PROPERTY(bool enableShaderEffects READ enableShaderEffects WRITE setEnableShaderEffects NOTIFY
                   enableShaderEffectsChanged)
    Q_PROPERTY(int shaderFrameRate READ shaderFrameRate WRITE setShaderFrameRate NOTIFY shaderFrameRateChanged)

    // Zones
    Q_PROPERTY(int zonePadding READ zonePadding WRITE setZonePadding NOTIFY zonePaddingChanged)
    Q_PROPERTY(int outerGap READ outerGap WRITE setOuterGap NOTIFY outerGapChanged)
    Q_PROPERTY(int adjacentThreshold READ adjacentThreshold WRITE setAdjacentThreshold NOTIFY adjacentThresholdChanged)

    // Behavior
    Q_PROPERTY(bool keepWindowsInZonesOnResolutionChange READ keepWindowsInZonesOnResolutionChange WRITE
                   setKeepWindowsInZonesOnResolutionChange NOTIFY keepWindowsInZonesOnResolutionChangeChanged)
    Q_PROPERTY(bool moveNewWindowsToLastZone READ moveNewWindowsToLastZone WRITE setMoveNewWindowsToLastZone NOTIFY
                   moveNewWindowsToLastZoneChanged)
    Q_PROPERTY(bool restoreOriginalSizeOnUnsnap READ restoreOriginalSizeOnUnsnap WRITE setRestoreOriginalSizeOnUnsnap
                   NOTIFY restoreOriginalSizeOnUnsnapChanged)
    Q_PROPERTY(int stickyWindowHandling READ stickyWindowHandling WRITE setStickyWindowHandling NOTIFY
                   stickyWindowHandlingChanged)
    Q_PROPERTY(bool restoreWindowsToZonesOnLogin READ restoreWindowsToZonesOnLogin WRITE setRestoreWindowsToZonesOnLogin
                   NOTIFY restoreWindowsToZonesOnLoginChanged)
    Q_PROPERTY(QString defaultLayoutId READ defaultLayoutId WRITE setDefaultLayoutId NOTIFY defaultLayoutIdChanged)

    // Exclusions
    Q_PROPERTY(QStringList excludedApplications READ excludedApplications WRITE setExcludedApplications NOTIFY
                   excludedApplicationsChanged)
    Q_PROPERTY(QStringList excludedWindowClasses READ excludedWindowClasses WRITE setExcludedWindowClasses NOTIFY
                   excludedWindowClassesChanged)
    Q_PROPERTY(bool excludeTransientWindows READ excludeTransientWindows WRITE setExcludeTransientWindows NOTIFY
                   excludeTransientWindowsChanged)
    Q_PROPERTY(
        int minimumWindowWidth READ minimumWindowWidth WRITE setMinimumWindowWidth NOTIFY minimumWindowWidthChanged)
    Q_PROPERTY(
        int minimumWindowHeight READ minimumWindowHeight WRITE setMinimumWindowHeight NOTIFY minimumWindowHeightChanged)

    // Zone selector
    Q_PROPERTY(bool zoneSelectorEnabled READ zoneSelectorEnabled WRITE setZoneSelectorEnabled NOTIFY
                   zoneSelectorEnabledChanged)
    Q_PROPERTY(int zoneSelectorTriggerDistance READ zoneSelectorTriggerDistance WRITE setZoneSelectorTriggerDistance
                   NOTIFY zoneSelectorTriggerDistanceChanged)
    Q_PROPERTY(int zoneSelectorPosition READ zoneSelectorPosition WRITE setZoneSelectorPosition NOTIFY
                   zoneSelectorPositionChanged)
    Q_PROPERTY(int zoneSelectorLayoutMode READ zoneSelectorLayoutMode WRITE setZoneSelectorLayoutMode NOTIFY
                   zoneSelectorLayoutModeChanged)
    Q_PROPERTY(int zoneSelectorPreviewWidth READ zoneSelectorPreviewWidth WRITE setZoneSelectorPreviewWidth NOTIFY
                   zoneSelectorPreviewWidthChanged)
    Q_PROPERTY(int zoneSelectorPreviewHeight READ zoneSelectorPreviewHeight WRITE setZoneSelectorPreviewHeight NOTIFY
                   zoneSelectorPreviewHeightChanged)
    Q_PROPERTY(bool zoneSelectorPreviewLockAspect READ zoneSelectorPreviewLockAspect WRITE
                   setZoneSelectorPreviewLockAspect NOTIFY zoneSelectorPreviewLockAspectChanged)
    Q_PROPERTY(int zoneSelectorGridColumns READ zoneSelectorGridColumns WRITE setZoneSelectorGridColumns NOTIFY
                   zoneSelectorGridColumnsChanged)
    Q_PROPERTY(int zoneSelectorSizeMode READ zoneSelectorSizeMode WRITE setZoneSelectorSizeMode NOTIFY
                   zoneSelectorSizeModeChanged)
    Q_PROPERTY(
        int zoneSelectorMaxRows READ zoneSelectorMaxRows WRITE setZoneSelectorMaxRows NOTIFY zoneSelectorMaxRowsChanged)

    // Editor shortcuts (app-specific only - standard shortcuts use Qt StandardKey)
    Q_PROPERTY(QString editorDuplicateShortcut READ editorDuplicateShortcut WRITE setEditorDuplicateShortcut NOTIFY
                   editorDuplicateShortcutChanged)
    Q_PROPERTY(QString editorSplitHorizontalShortcut READ editorSplitHorizontalShortcut WRITE
                   setEditorSplitHorizontalShortcut NOTIFY editorSplitHorizontalShortcutChanged)
    Q_PROPERTY(QString editorSplitVerticalShortcut READ editorSplitVerticalShortcut WRITE setEditorSplitVerticalShortcut
                   NOTIFY editorSplitVerticalShortcutChanged)
    Q_PROPERTY(
        QString editorFillShortcut READ editorFillShortcut WRITE setEditorFillShortcut NOTIFY editorFillShortcutChanged)

    // Editor snapping settings
    Q_PROPERTY(bool editorGridSnappingEnabled READ editorGridSnappingEnabled WRITE setEditorGridSnappingEnabled NOTIFY
                   editorGridSnappingEnabledChanged)
    Q_PROPERTY(bool editorEdgeSnappingEnabled READ editorEdgeSnappingEnabled WRITE setEditorEdgeSnappingEnabled NOTIFY
                   editorEdgeSnappingEnabledChanged)
    Q_PROPERTY(qreal editorSnapIntervalX READ editorSnapIntervalX WRITE setEditorSnapIntervalX NOTIFY
                   editorSnapIntervalXChanged)
    Q_PROPERTY(qreal editorSnapIntervalY READ editorSnapIntervalY WRITE setEditorSnapIntervalY NOTIFY
                   editorSnapIntervalYChanged)
    Q_PROPERTY(int editorSnapOverrideModifier READ editorSnapOverrideModifier WRITE setEditorSnapOverrideModifier NOTIFY
                   editorSnapOverrideModifierChanged)

    // Fill on drop
    Q_PROPERTY(bool fillOnDropEnabled READ fillOnDropEnabled WRITE setFillOnDropEnabled NOTIFY fillOnDropEnabledChanged)
    Q_PROPERTY(
        int fillOnDropModifier READ fillOnDropModifier WRITE setFillOnDropModifier NOTIFY fillOnDropModifierChanged)

    // Layouts
    Q_PROPERTY(QVariantList layouts READ layouts NOTIFY layoutsChanged)
    Q_PROPERTY(QString layoutToSelect READ layoutToSelect NOTIFY layoutToSelectChanged)

    // Screens and assignments
    Q_PROPERTY(QVariantList screens READ screens NOTIFY screensChanged)
    Q_PROPERTY(QVariantMap screenAssignments READ screenAssignments NOTIFY screenAssignmentsChanged)

    // Virtual desktop support
    Q_PROPERTY(int virtualDesktopCount READ virtualDesktopCount NOTIFY virtualDesktopCountChanged)
    Q_PROPERTY(QStringList virtualDesktopNames READ virtualDesktopNames NOTIFY virtualDesktopNamesChanged)

    // KDE Activities support
    Q_PROPERTY(bool activitiesAvailable READ activitiesAvailable NOTIFY activitiesAvailableChanged)
    Q_PROPERTY(QVariantList activities READ activities NOTIFY activitiesChanged)
    Q_PROPERTY(QString currentActivity READ currentActivity NOTIFY currentActivityChanged)

    // Autotiling settings
    Q_PROPERTY(bool autotileEnabled READ autotileEnabled WRITE setAutotileEnabled NOTIFY autotileEnabledChanged)
    Q_PROPERTY(QString autotileAlgorithm READ autotileAlgorithm WRITE setAutotileAlgorithm NOTIFY
                   autotileAlgorithmChanged)
    Q_PROPERTY(
        qreal autotileSplitRatio READ autotileSplitRatio WRITE setAutotileSplitRatio NOTIFY autotileSplitRatioChanged)
    Q_PROPERTY(
        int autotileMasterCount READ autotileMasterCount WRITE setAutotileMasterCount NOTIFY autotileMasterCountChanged)
    Q_PROPERTY(int autotileInnerGap READ autotileInnerGap WRITE setAutotileInnerGap NOTIFY autotileInnerGapChanged)
    Q_PROPERTY(int autotileOuterGap READ autotileOuterGap WRITE setAutotileOuterGap NOTIFY autotileOuterGapChanged)
    Q_PROPERTY(bool autotileFocusNewWindows READ autotileFocusNewWindows WRITE setAutotileFocusNewWindows NOTIFY
                   autotileFocusNewWindowsChanged)
    Q_PROPERTY(bool autotileSmartGaps READ autotileSmartGaps WRITE setAutotileSmartGaps NOTIFY autotileSmartGapsChanged)
    Q_PROPERTY(int autotileInsertPosition READ autotileInsertPosition WRITE setAutotileInsertPosition NOTIFY
                   autotileInsertPositionChanged)
    Q_PROPERTY(bool autotileFocusFollowsMouse READ autotileFocusFollowsMouse WRITE setAutotileFocusFollowsMouse NOTIFY
                   autotileFocusFollowsMouseChanged)
    Q_PROPERTY(bool autotileRespectMinimumSize READ autotileRespectMinimumSize WRITE setAutotileRespectMinimumSize NOTIFY
                   autotileRespectMinimumSizeChanged)
    Q_PROPERTY(bool autotileShowActiveBorder READ autotileShowActiveBorder WRITE setAutotileShowActiveBorder NOTIFY
                   autotileShowActiveBorderChanged)
    Q_PROPERTY(int autotileActiveBorderWidth READ autotileActiveBorderWidth WRITE setAutotileActiveBorderWidth NOTIFY
                   autotileActiveBorderWidthChanged)
    Q_PROPERTY(bool autotileUseSystemBorderColor READ autotileUseSystemBorderColor WRITE setAutotileUseSystemBorderColor
                   NOTIFY autotileUseSystemBorderColorChanged)
    Q_PROPERTY(QColor autotileActiveBorderColor READ autotileActiveBorderColor WRITE setAutotileActiveBorderColor NOTIFY
                   autotileActiveBorderColorChanged)
    Q_PROPERTY(bool autotileMonocleHideOthers READ autotileMonocleHideOthers WRITE setAutotileMonocleHideOthers NOTIFY
                   autotileMonocleHideOthersChanged)
    Q_PROPERTY(bool autotileMonocleShowTabs READ autotileMonocleShowTabs WRITE setAutotileMonocleShowTabs NOTIFY
                   autotileMonocleShowTabsChanged)
    Q_PROPERTY(bool autotileAnimationsEnabled READ autotileAnimationsEnabled WRITE setAutotileAnimationsEnabled NOTIFY
                   autotileAnimationsEnabledChanged)
    Q_PROPERTY(int autotileAnimationDuration READ autotileAnimationDuration WRITE setAutotileAnimationDuration NOTIFY
                   autotileAnimationDurationChanged)

    // Autotiling shortcuts
    Q_PROPERTY(QString autotileToggleShortcut READ autotileToggleShortcut WRITE setAutotileToggleShortcut NOTIFY
                   autotileToggleShortcutChanged)
    Q_PROPERTY(QString autotileFocusMasterShortcut READ autotileFocusMasterShortcut WRITE setAutotileFocusMasterShortcut
                   NOTIFY autotileFocusMasterShortcutChanged)
    Q_PROPERTY(QString autotileSwapMasterShortcut READ autotileSwapMasterShortcut WRITE setAutotileSwapMasterShortcut
                   NOTIFY autotileSwapMasterShortcutChanged)
    Q_PROPERTY(QString autotileIncMasterRatioShortcut READ autotileIncMasterRatioShortcut WRITE
                   setAutotileIncMasterRatioShortcut NOTIFY autotileIncMasterRatioShortcutChanged)
    Q_PROPERTY(QString autotileDecMasterRatioShortcut READ autotileDecMasterRatioShortcut WRITE
                   setAutotileDecMasterRatioShortcut NOTIFY autotileDecMasterRatioShortcutChanged)
    Q_PROPERTY(QString autotileIncMasterCountShortcut READ autotileIncMasterCountShortcut WRITE
                   setAutotileIncMasterCountShortcut NOTIFY autotileIncMasterCountShortcutChanged)
    Q_PROPERTY(QString autotileDecMasterCountShortcut READ autotileDecMasterCountShortcut WRITE
                   setAutotileDecMasterCountShortcut NOTIFY autotileDecMasterCountShortcutChanged)
    Q_PROPERTY(QString autotileRetileShortcut READ autotileRetileShortcut WRITE setAutotileRetileShortcut NOTIFY
                   autotileRetileShortcutChanged)

    // Daemon status
    Q_PROPERTY(bool daemonRunning READ isDaemonRunning NOTIFY daemonRunningChanged)
    Q_PROPERTY(bool daemonEnabled READ isDaemonEnabled WRITE setDaemonEnabled NOTIFY daemonEnabledChanged)

public:
    explicit KCMPlasmaZones(QObject* parent, const KPluginMetaData& data);
    ~KCMPlasmaZones() override;

    // Property getters
    bool shiftDragToActivate() const;
    int dragActivationModifier() const;
    int multiZoneModifier() const;
    bool middleClickMultiZone() const;
    bool showZonesOnAllMonitors() const;
    QStringList disabledMonitors() const;
    bool showZoneNumbers() const;
    bool flashZonesOnSwitch() const;
    bool showOsdOnLayoutSwitch() const;
    bool showNavigationOsd() const;
    int osdStyle() const;
    bool useSystemColors() const;
    QColor highlightColor() const;
    QColor inactiveColor() const;
    QColor borderColor() const;
    QColor numberColor() const;
    qreal activeOpacity() const;
    qreal inactiveOpacity() const;
    int borderWidth() const;
    int borderRadius() const;
    bool enableBlur() const;
    bool enableShaderEffects() const;
    int shaderFrameRate() const;
    int zonePadding() const;
    int outerGap() const;
    int adjacentThreshold() const;
    bool keepWindowsInZonesOnResolutionChange() const;
    bool moveNewWindowsToLastZone() const;
    bool restoreOriginalSizeOnUnsnap() const;
    int stickyWindowHandling() const;
    bool restoreWindowsToZonesOnLogin() const;
    QString defaultLayoutId() const;
    QStringList excludedApplications() const;
    QStringList excludedWindowClasses() const;
    bool excludeTransientWindows() const;
    int minimumWindowWidth() const;
    int minimumWindowHeight() const;
    bool zoneSelectorEnabled() const;
    int zoneSelectorTriggerDistance() const;
    int zoneSelectorPosition() const;
    int zoneSelectorLayoutMode() const;
    int zoneSelectorPreviewWidth() const;
    int zoneSelectorPreviewHeight() const;
    bool zoneSelectorPreviewLockAspect() const;
    int zoneSelectorGridColumns() const;
    int zoneSelectorSizeMode() const;
    int zoneSelectorMaxRows() const;
    QString editorDuplicateShortcut() const;
    QString editorSplitHorizontalShortcut() const;
    QString editorSplitVerticalShortcut() const;
    QString editorFillShortcut() const;
    bool editorGridSnappingEnabled() const;
    bool editorEdgeSnappingEnabled() const;
    qreal editorSnapIntervalX() const;
    qreal editorSnapIntervalY() const;
    int editorSnapOverrideModifier() const;
    bool fillOnDropEnabled() const;
    int fillOnDropModifier() const;
    QVariantList layouts() const;
    QString layoutToSelect() const;
    QVariantList screens() const;
    QVariantMap screenAssignments() const;

    // Autotiling getters
    bool autotileEnabled() const;
    QString autotileAlgorithm() const;
    qreal autotileSplitRatio() const;
    int autotileMasterCount() const;
    int autotileInnerGap() const;
    int autotileOuterGap() const;
    bool autotileFocusNewWindows() const;
    bool autotileSmartGaps() const;
    int autotileInsertPosition() const;
    bool autotileFocusFollowsMouse() const;
    bool autotileRespectMinimumSize() const;
    bool autotileShowActiveBorder() const;
    int autotileActiveBorderWidth() const;
    bool autotileUseSystemBorderColor() const;
    QColor autotileActiveBorderColor() const;
    bool autotileMonocleHideOthers() const;
    bool autotileMonocleShowTabs() const;
    bool autotileAnimationsEnabled() const;
    int autotileAnimationDuration() const;
    QString autotileToggleShortcut() const;
    QString autotileFocusMasterShortcut() const;
    QString autotileSwapMasterShortcut() const;
    QString autotileIncMasterRatioShortcut() const;
    QString autotileDecMasterRatioShortcut() const;
    QString autotileIncMasterCountShortcut() const;
    QString autotileDecMasterCountShortcut() const;
    QString autotileRetileShortcut() const;

    // Virtual desktop support
    int virtualDesktopCount() const;
    QStringList virtualDesktopNames() const;

    // KDE Activities support
    bool activitiesAvailable() const;
    QVariantList activities() const;
    QString currentActivity() const;

    // Daemon status
    bool isDaemonRunning() const;
    bool isDaemonEnabled() const;
    void setDaemonEnabled(bool enabled);

    // Property setters
    void setShiftDragToActivate(bool enable);
    void setDragActivationModifier(int modifier);
    void setMultiZoneModifier(int modifier);
    void setMiddleClickMultiZone(bool enable);
    void setShowZonesOnAllMonitors(bool show);
    void setShowZoneNumbers(bool show);
    void setFlashZonesOnSwitch(bool flash);
    void setShowOsdOnLayoutSwitch(bool show);
    void setShowNavigationOsd(bool show);
    void setOsdStyle(int style);
    void setUseSystemColors(bool use);
    void setHighlightColor(const QColor& color);
    void setInactiveColor(const QColor& color);
    void setBorderColor(const QColor& color);
    void setNumberColor(const QColor& color);
    void setActiveOpacity(qreal opacity);
    void setInactiveOpacity(qreal opacity);
    void setBorderWidth(int width);
    void setBorderRadius(int radius);
    void setEnableBlur(bool enable);
    void setEnableShaderEffects(bool enable);
    void setShaderFrameRate(int fps);
    void setZonePadding(int padding);
    void setOuterGap(int gap);
    void setAdjacentThreshold(int threshold);
    void setKeepWindowsInZonesOnResolutionChange(bool keep);
    void setMoveNewWindowsToLastZone(bool move);
    void setRestoreOriginalSizeOnUnsnap(bool restore);
    void setStickyWindowHandling(int handling);
    void setRestoreWindowsToZonesOnLogin(bool restore);
    void setDefaultLayoutId(const QString& layoutId);
    void setExcludedApplications(const QStringList& apps);
    void setExcludedWindowClasses(const QStringList& classes);
    void setExcludeTransientWindows(bool exclude);
    void setMinimumWindowWidth(int width);
    void setMinimumWindowHeight(int height);
    void setZoneSelectorEnabled(bool enabled);
    void setZoneSelectorTriggerDistance(int distance);
    void setZoneSelectorPosition(int position);
    void setZoneSelectorLayoutMode(int mode);
    void setZoneSelectorPreviewWidth(int width);
    void setZoneSelectorPreviewHeight(int height);
    void setZoneSelectorPreviewLockAspect(bool locked);
    void setZoneSelectorGridColumns(int columns);
    void setZoneSelectorSizeMode(int mode);
    void setZoneSelectorMaxRows(int rows);
    void setEditorDuplicateShortcut(const QString& shortcut);
    void setEditorSplitHorizontalShortcut(const QString& shortcut);
    void setEditorSplitVerticalShortcut(const QString& shortcut);
    void setEditorFillShortcut(const QString& shortcut);
    void setEditorGridSnappingEnabled(bool enabled);
    void setEditorEdgeSnappingEnabled(bool enabled);
    void setEditorSnapIntervalX(qreal interval);
    void setEditorSnapIntervalY(qreal interval);
    void setEditorSnapOverrideModifier(int modifier);
    void setFillOnDropEnabled(bool enabled);
    void setFillOnDropModifier(int modifier);

    // Autotiling setters
    void setAutotileEnabled(bool enabled);
    void setAutotileAlgorithm(const QString& algorithm);
    void setAutotileSplitRatio(qreal ratio);
    void setAutotileMasterCount(int count);
    void setAutotileInnerGap(int gap);
    void setAutotileOuterGap(int gap);
    void setAutotileFocusNewWindows(bool focus);
    void setAutotileSmartGaps(bool smart);
    void setAutotileInsertPosition(int position);
    void setAutotileFocusFollowsMouse(bool follows);
    void setAutotileRespectMinimumSize(bool respect);
    void setAutotileShowActiveBorder(bool show);
    void setAutotileActiveBorderWidth(int width);
    void setAutotileUseSystemBorderColor(bool use);
    void setAutotileActiveBorderColor(const QColor& color);
    void setAutotileMonocleHideOthers(bool hide);
    void setAutotileMonocleShowTabs(bool show);
    void setAutotileAnimationsEnabled(bool enabled);
    void setAutotileAnimationDuration(int duration);
    void setAutotileToggleShortcut(const QString& shortcut);
    void setAutotileFocusMasterShortcut(const QString& shortcut);
    void setAutotileSwapMasterShortcut(const QString& shortcut);
    void setAutotileIncMasterRatioShortcut(const QString& shortcut);
    void setAutotileDecMasterRatioShortcut(const QString& shortcut);
    void setAutotileIncMasterCountShortcut(const QString& shortcut);
    void setAutotileDecMasterCountShortcut(const QString& shortcut);
    void setAutotileRetileShortcut(const QString& shortcut);

public Q_SLOTS:
    void save() override;
    void load() override;
    void defaults() override;

    // Layout management
    Q_INVOKABLE void createNewLayout();
    Q_INVOKABLE void deleteLayout(const QString& layoutId);
    Q_INVOKABLE void duplicateLayout(const QString& layoutId);
    Q_INVOKABLE void importLayout(const QString& filePath);
    Q_INVOKABLE void exportLayout(const QString& layoutId, const QString& filePath);
    Q_INVOKABLE void editLayout(const QString& layoutId);
    Q_INVOKABLE void openEditor();

    // Exclusion management
    Q_INVOKABLE void addExcludedApp(const QString& app);
    Q_INVOKABLE void removeExcludedApp(int index);
    Q_INVOKABLE void addExcludedWindowClass(const QString& windowClass);
    Q_INVOKABLE void removeExcludedWindowClass(int index);

    // Pywal integration
    Q_INVOKABLE void loadColorsFromPywal();
    Q_INVOKABLE void loadColorsFromFile(const QString& filePath);

    // Editor shortcuts reset
    Q_INVOKABLE void resetEditorShortcuts();

    // Autotiling
    Q_INVOKABLE QVariantList availableAlgorithms() const;

    // Screen assignments (legacy - applies to all desktops)
    Q_INVOKABLE void assignLayoutToScreen(const QString& screenName, const QString& layoutId);
    Q_INVOKABLE void clearScreenAssignment(const QString& screenName);
    Q_INVOKABLE QString getLayoutForScreen(const QString& screenName) const;

    // Per-virtual-desktop screen assignments
    Q_INVOKABLE void assignLayoutToScreenDesktop(const QString& screenName, int virtualDesktop,
                                                 const QString& layoutId);
    Q_INVOKABLE void clearScreenDesktopAssignment(const QString& screenName, int virtualDesktop);
    Q_INVOKABLE QString getLayoutForScreenDesktop(const QString& screenName, int virtualDesktop) const;
    Q_INVOKABLE bool hasExplicitAssignmentForScreenDesktop(const QString& screenName, int virtualDesktop) const;
    Q_INVOKABLE QString getAllScreenAssignmentsJson() const;

    // Per-monitor disable (no overlay, no zone picker, no snapping on that monitor)
    Q_INVOKABLE bool isMonitorDisabled(const QString& screenName) const;
    Q_INVOKABLE void setMonitorDisabled(const QString& screenName, bool disabled);

    // Quick layout slots (1-9)
    Q_INVOKABLE QString getQuickLayoutSlot(int slotNumber) const;
    Q_INVOKABLE void setQuickLayoutSlot(int slotNumber, const QString& layoutId);
    Q_INVOKABLE QString getQuickLayoutShortcut(int slotNumber) const;

    // Per-activity screen assignments
    Q_INVOKABLE void assignLayoutToScreenActivity(const QString& screenName, const QString& activityId,
                                                   const QString& layoutId);
    Q_INVOKABLE void clearScreenActivityAssignment(const QString& screenName, const QString& activityId);
    Q_INVOKABLE QString getLayoutForScreenActivity(const QString& screenName, const QString& activityId) const;
    Q_INVOKABLE bool hasExplicitAssignmentForScreenActivity(const QString& screenName, const QString& activityId) const;
    Q_INVOKABLE QString getActivityName(const QString& activityId) const;
    Q_INVOKABLE QString getActivityIcon(const QString& activityId) const;

    // Daemon control
    Q_INVOKABLE void startDaemon();
    Q_INVOKABLE void stopDaemon();

Q_SIGNALS:
    void shiftDragToActivateChanged();
    void dragActivationModifierChanged();
    void multiZoneModifierChanged();
    void middleClickMultiZoneChanged();
    void showZonesOnAllMonitorsChanged();
    void disabledMonitorsChanged();
    void showZoneNumbersChanged();
    void flashZonesOnSwitchChanged();
    void showOsdOnLayoutSwitchChanged();
    void showNavigationOsdChanged();
    void osdStyleChanged();
    void useSystemColorsChanged();
    void highlightColorChanged();
    void inactiveColorChanged();
    void borderColorChanged();
    void numberColorChanged();
    void activeOpacityChanged();
    void inactiveOpacityChanged();
    void borderWidthChanged();
    void borderRadiusChanged();
    void enableBlurChanged();
    void enableShaderEffectsChanged();
    void shaderFrameRateChanged();
    void zonePaddingChanged();
    void outerGapChanged();
    void adjacentThresholdChanged();
    void keepWindowsInZonesOnResolutionChangeChanged();
    void moveNewWindowsToLastZoneChanged();
    void restoreOriginalSizeOnUnsnapChanged();
    void stickyWindowHandlingChanged();
    void restoreWindowsToZonesOnLoginChanged();
    void defaultLayoutIdChanged();
    void excludedApplicationsChanged();
    void excludedWindowClassesChanged();
    void excludeTransientWindowsChanged();
    void minimumWindowWidthChanged();
    void minimumWindowHeightChanged();
    void zoneSelectorEnabledChanged();
    void zoneSelectorTriggerDistanceChanged();
    void zoneSelectorPositionChanged();
    void zoneSelectorLayoutModeChanged();
    void zoneSelectorPreviewWidthChanged();
    void zoneSelectorPreviewHeightChanged();
    void zoneSelectorPreviewLockAspectChanged();
    void zoneSelectorGridColumnsChanged();
    void zoneSelectorSizeModeChanged();
    void zoneSelectorMaxRowsChanged();
    void editorDuplicateShortcutChanged();
    void editorSplitHorizontalShortcutChanged();
    void editorSplitVerticalShortcutChanged();
    void editorFillShortcutChanged();
    void editorGridSnappingEnabledChanged();
    void editorEdgeSnappingEnabledChanged();
    void editorSnapIntervalXChanged();
    void editorSnapIntervalYChanged();
    void editorSnapOverrideModifierChanged();
    void fillOnDropEnabledChanged();
    void fillOnDropModifierChanged();

    // Autotiling signals
    void autotileEnabledChanged();
    void autotileAlgorithmChanged();
    void autotileSplitRatioChanged();
    void autotileMasterCountChanged();
    void autotileInnerGapChanged();
    void autotileOuterGapChanged();
    void autotileFocusNewWindowsChanged();
    void autotileSmartGapsChanged();
    void autotileInsertPositionChanged();
    void autotileFocusFollowsMouseChanged();
    void autotileRespectMinimumSizeChanged();
    void autotileShowActiveBorderChanged();
    void autotileActiveBorderWidthChanged();
    void autotileUseSystemBorderColorChanged();
    void autotileActiveBorderColorChanged();
    void autotileMonocleHideOthersChanged();
    void autotileMonocleShowTabsChanged();
    void autotileAnimationsEnabledChanged();
    void autotileAnimationDurationChanged();
    void autotileToggleShortcutChanged();
    void autotileFocusMasterShortcutChanged();
    void autotileSwapMasterShortcutChanged();
    void autotileIncMasterRatioShortcutChanged();
    void autotileDecMasterRatioShortcutChanged();
    void autotileIncMasterCountShortcutChanged();
    void autotileDecMasterCountShortcutChanged();
    void autotileRetileShortcutChanged();

    void layoutsChanged();
    void layoutToSelectChanged();
    void screensChanged();
    void screenAssignmentsChanged();
    void virtualDesktopCountChanged();
    void virtualDesktopNamesChanged();
    void activitiesAvailableChanged();
    void activitiesChanged();
    void currentActivityChanged();
    void activityAssignmentsChanged();
    void daemonRunningChanged();
    void daemonEnabledChanged();
    void quickLayoutSlotsRefreshed(); // Emitted when quick layout slots are reloaded from daemon

private Q_SLOTS:
    void loadLayouts();
    void refreshScreens();
    void checkDaemonStatus();
    void refreshVirtualDesktops();
    void refreshActivities();
    void onActiveLayoutIdChanged(const QString& layoutId);
    void onScreenLayoutChanged(const QString& screenName, const QString& layoutId);
    void onQuickLayoutSlotsChanged();
    void onSettingsChanged();
    void onCurrentActivityChanged(const QString& activityId);
    void onActivitiesChanged();

private:
    void notifyDaemon();
    void refreshDaemonEnabledState(); // Async refresh of systemd enabled state
    void setDaemonAutostart(bool enabled);

    // Async systemctl helper - runs command and calls callback with (success, output)
    using SystemctlCallback = std::function<void(bool success, const QString& output)>;
    void runSystemctl(const QStringList& args, SystemctlCallback callback = nullptr);

    // D-Bus helper for calls with timeout and error handling
    // Returns the reply message; check reply.type() == QDBusMessage::ErrorMessage for errors
    static constexpr int DBusTimeoutMs = 5000; // 5 second timeout
    QDBusMessage callDaemon(const QString& interface, const QString& method, const QVariantList& args = {}) const;

    /**
     * @brief Get Editor config group from plasmazonesrc
     * @return KConfigGroup for the Editor section
     */
    static KConfigGroup editorConfigGroup();

    Settings* m_settings = nullptr;
    QTimer* m_daemonCheckTimer = nullptr;
    bool m_daemonEnabled = true;
    bool m_lastDaemonState = false;
    QVariantList m_layouts;
    QString m_layoutToSelect; // Layout ID to select after layouts are reloaded

    // Screens and assignments
    QVariantList m_screens;
    QVariantMap m_screenAssignments; // screenName -> layoutId (for virtualDesktop=0, all desktops)
    QMap<int, QString> m_quickLayoutSlots; // slotNumber (1-9) -> layoutId

    // Virtual desktop support
    int m_virtualDesktopCount = 1;
    QStringList m_virtualDesktopNames;

    // KDE Activities support
    bool m_activitiesAvailable = false;
    QVariantList m_activities;
    QString m_currentActivity;

    // Pending per-activity assignments (not yet applied)
    // Key format: "screenName:activityId" -> layoutId (empty string means cleared)
    QMap<QString, QString> m_pendingActivityAssignments;
    QSet<QString> m_clearedActivityAssignments; // Keys that should be cleared on save

    // Pending per-desktop assignments (not yet applied)
    // Key format: "screenName:desktopNumber" -> layoutId (empty string means cleared)
    QMap<QString, QString> m_pendingDesktopAssignments;
    QSet<QString> m_clearedDesktopAssignments; // Keys that should be cleared on save

    // Fill on drop settings (stored separately in Editor group)
    bool m_fillOnDropEnabled = true;
    int m_fillOnDropModifier = 0x04000000; // Qt::ControlModifier

    // Save guard to prevent re-entry during synchronous D-Bus operations
    bool m_saveInProgress = false;
};

} // namespace PlasmaZones
