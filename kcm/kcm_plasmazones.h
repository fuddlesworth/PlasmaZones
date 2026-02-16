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
#include <QDBusPendingCall>
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
class UpdateChecker;

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
    Q_PROPERTY(QVariantList dragActivationTriggers READ dragActivationTriggers WRITE setDragActivationTriggers NOTIFY
                   dragActivationTriggersChanged)
    Q_PROPERTY(bool zoneSpanEnabled READ zoneSpanEnabled WRITE setZoneSpanEnabled NOTIFY zoneSpanEnabledChanged)
    Q_PROPERTY(QVariantList zoneSpanTriggers READ zoneSpanTriggers WRITE setZoneSpanTriggers NOTIFY
                   zoneSpanTriggersChanged)
    Q_PROPERTY(bool toggleActivation READ toggleActivation WRITE setToggleActivation NOTIFY
                   toggleActivationChanged)

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
    Q_PROPERTY(QColor labelFontColor READ labelFontColor WRITE setLabelFontColor NOTIFY labelFontColorChanged)
    Q_PROPERTY(qreal activeOpacity READ activeOpacity WRITE setActiveOpacity NOTIFY activeOpacityChanged)
    Q_PROPERTY(qreal inactiveOpacity READ inactiveOpacity WRITE setInactiveOpacity NOTIFY inactiveOpacityChanged)
    Q_PROPERTY(int borderWidth READ borderWidth WRITE setBorderWidth NOTIFY borderWidthChanged)
    Q_PROPERTY(int borderRadius READ borderRadius WRITE setBorderRadius NOTIFY borderRadiusChanged)
    Q_PROPERTY(bool enableBlur READ enableBlur WRITE setEnableBlur NOTIFY enableBlurChanged)
    Q_PROPERTY(QString labelFontFamily READ labelFontFamily WRITE setLabelFontFamily NOTIFY labelFontFamilyChanged)
    Q_PROPERTY(qreal labelFontSizeScale READ labelFontSizeScale WRITE setLabelFontSizeScale NOTIFY labelFontSizeScaleChanged)
    Q_PROPERTY(int labelFontWeight READ labelFontWeight WRITE setLabelFontWeight NOTIFY labelFontWeightChanged)
    Q_PROPERTY(bool labelFontItalic READ labelFontItalic WRITE setLabelFontItalic NOTIFY labelFontItalicChanged)
    Q_PROPERTY(bool labelFontUnderline READ labelFontUnderline WRITE setLabelFontUnderline NOTIFY labelFontUnderlineChanged)
    Q_PROPERTY(bool labelFontStrikeout READ labelFontStrikeout WRITE setLabelFontStrikeout NOTIFY labelFontStrikeoutChanged)

    // Shader Effects
    Q_PROPERTY(bool enableShaderEffects READ enableShaderEffects WRITE setEnableShaderEffects NOTIFY
                   enableShaderEffectsChanged)
    Q_PROPERTY(int shaderFrameRate READ shaderFrameRate WRITE setShaderFrameRate NOTIFY shaderFrameRateChanged)
    Q_PROPERTY(bool enableAudioVisualizer READ enableAudioVisualizer WRITE setEnableAudioVisualizer NOTIFY
                   enableAudioVisualizerChanged)
    Q_PROPERTY(bool cavaAvailable READ cavaAvailable CONSTANT)
    Q_PROPERTY(int audioSpectrumBarCount READ audioSpectrumBarCount WRITE setAudioSpectrumBarCount NOTIFY
                   audioSpectrumBarCountChanged)

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
    Q_PROPERTY(bool snapAssistFeatureEnabled READ snapAssistFeatureEnabled WRITE setSnapAssistFeatureEnabled NOTIFY snapAssistFeatureEnabledChanged)
    Q_PROPERTY(bool snapAssistEnabled READ snapAssistEnabled WRITE setSnapAssistEnabled NOTIFY snapAssistEnabledChanged)
    Q_PROPERTY(QVariantList snapAssistTriggers READ snapAssistTriggers WRITE setSnapAssistTriggers NOTIFY snapAssistTriggersChanged)
    Q_PROPERTY(QVariantList defaultSnapAssistTriggers READ defaultSnapAssistTriggers CONSTANT)
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

    // Default values for reset-to-default in UI components (CONSTANT — never change at runtime)
    Q_PROPERTY(QVariantList defaultDragActivationTriggers READ defaultDragActivationTriggers CONSTANT)
    Q_PROPERTY(QVariantList defaultZoneSpanTriggers READ defaultZoneSpanTriggers CONSTANT)
    Q_PROPERTY(int defaultEditorSnapOverrideModifier READ defaultEditorSnapOverrideModifier CONSTANT)
    Q_PROPERTY(int defaultFillOnDropModifier READ defaultFillOnDropModifier CONSTANT)
    Q_PROPERTY(QString defaultEditorDuplicateShortcut READ defaultEditorDuplicateShortcut CONSTANT)
    Q_PROPERTY(QString defaultEditorSplitHorizontalShortcut READ defaultEditorSplitHorizontalShortcut CONSTANT)
    Q_PROPERTY(QString defaultEditorSplitVerticalShortcut READ defaultEditorSplitVerticalShortcut CONSTANT)
    Q_PROPERTY(QString defaultEditorFillShortcut READ defaultEditorFillShortcut CONSTANT)

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

    // Daemon status
    Q_PROPERTY(bool daemonRunning READ isDaemonRunning NOTIFY daemonRunningChanged)
    Q_PROPERTY(bool daemonEnabled READ isDaemonEnabled WRITE setDaemonEnabled NOTIFY daemonEnabledChanged)

    // Update checker
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY updateAvailableChanged)
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY latestVersionChanged)
    Q_PROPERTY(QString releaseUrl READ releaseUrl NOTIFY releaseUrlChanged)
    Q_PROPERTY(bool checkingForUpdates READ checkingForUpdates NOTIFY checkingForUpdatesChanged)
    Q_PROPERTY(QString dismissedUpdateVersion READ dismissedUpdateVersion WRITE setDismissedUpdateVersion NOTIFY dismissedUpdateVersionChanged)

public:
    explicit KCMPlasmaZones(QObject* parent, const KPluginMetaData& data);
    ~KCMPlasmaZones() override;

    // Property getters
    QVariantList dragActivationTriggers() const;
    bool zoneSpanEnabled() const;
    QVariantList zoneSpanTriggers() const;
    bool toggleActivation() const;
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
    QColor labelFontColor() const;
    qreal activeOpacity() const;
    qreal inactiveOpacity() const;
    int borderWidth() const;
    int borderRadius() const;
    bool enableBlur() const;
    QString labelFontFamily() const;
    qreal labelFontSizeScale() const;
    int labelFontWeight() const;
    bool labelFontItalic() const;
    bool labelFontUnderline() const;
    bool labelFontStrikeout() const;
    bool enableShaderEffects() const;
    int shaderFrameRate() const;
    bool enableAudioVisualizer() const;
    bool cavaAvailable() const;
    int audioSpectrumBarCount() const;
    int zonePadding() const;
    int outerGap() const;
    int adjacentThreshold() const;
    bool keepWindowsInZonesOnResolutionChange() const;
    bool moveNewWindowsToLastZone() const;
    bool restoreOriginalSizeOnUnsnap() const;
    int stickyWindowHandling() const;
    bool restoreWindowsToZonesOnLogin() const;
    bool snapAssistFeatureEnabled() const;
    bool snapAssistEnabled() const;
    QVariantList snapAssistTriggers() const;
    QVariantList defaultSnapAssistTriggers() const;
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

    // Default value getters (for reset-to-default buttons in UI)
    QVariantList defaultDragActivationTriggers() const;
    QVariantList defaultZoneSpanTriggers() const;
    int defaultEditorSnapOverrideModifier() const;
    int defaultFillOnDropModifier() const;
    QString defaultEditorDuplicateShortcut() const;
    QString defaultEditorSplitHorizontalShortcut() const;
    QString defaultEditorSplitVerticalShortcut() const;
    QString defaultEditorFillShortcut() const;
    QVariantList layouts() const;
    QString layoutToSelect() const;
    QVariantList screens() const;
    QVariantMap screenAssignments() const;

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

    // Update checker
    bool updateAvailable() const;
    QString currentVersion() const;
    QString latestVersion() const;
    QString releaseUrl() const;
    bool checkingForUpdates() const;
    QString dismissedUpdateVersion() const;
    void setDismissedUpdateVersion(const QString& version);

    // Property setters
    void setDragActivationTriggers(const QVariantList& triggers);
    void setZoneSpanEnabled(bool enabled);
    void setZoneSpanTriggers(const QVariantList& triggers);
    void setToggleActivation(bool enable);
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
    void setLabelFontColor(const QColor& color);
    void setActiveOpacity(qreal opacity);
    void setInactiveOpacity(qreal opacity);
    void setBorderWidth(int width);
    void setBorderRadius(int radius);
    void setEnableBlur(bool enable);
    void setLabelFontFamily(const QString& family);
    void setLabelFontSizeScale(qreal scale);
    void setLabelFontWeight(int weight);
    void setLabelFontItalic(bool italic);
    void setLabelFontUnderline(bool underline);
    void setLabelFontStrikeout(bool strikeout);
    void setEnableShaderEffects(bool enable);
    void setShaderFrameRate(int fps);
    void setEnableAudioVisualizer(bool enable);
    void setAudioSpectrumBarCount(int count);
    void setZonePadding(int padding);
    void setOuterGap(int gap);
    void setAdjacentThreshold(int threshold);
    void setKeepWindowsInZonesOnResolutionChange(bool keep);
    void setMoveNewWindowsToLastZone(bool move);
    void setRestoreOriginalSizeOnUnsnap(bool restore);
    void setStickyWindowHandling(int handling);
    void setRestoreWindowsToZonesOnLogin(bool restore);
    void setSnapAssistFeatureEnabled(bool enabled);
    void setSnapAssistEnabled(bool enabled);
    void setSnapAssistTriggers(const QVariantList& triggers);
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
    Q_INVOKABLE void setLayoutHidden(const QString& layoutId, bool hidden);
    Q_INVOKABLE void setLayoutAutoAssign(const QString& layoutId, bool enabled);

    // App-to-zone rules management
    Q_INVOKABLE QVariantList getAppRulesForLayout(const QString& layoutId) const;
    Q_INVOKABLE void setAppRulesForLayout(const QString& layoutId, const QVariantList& rules);
    Q_INVOKABLE void addAppRuleToLayout(const QString& layoutId, const QString& pattern,
                                         int zoneNumber, const QString& targetScreen = QString());
    Q_INVOKABLE void removeAppRuleFromLayout(const QString& layoutId, int index);

    // Exclusion management
    Q_INVOKABLE void addExcludedApp(const QString& app);
    Q_INVOKABLE void removeExcludedApp(int index);
    Q_INVOKABLE void addExcludedWindowClass(const QString& windowClass);
    Q_INVOKABLE void removeExcludedWindowClass(int index);
    Q_INVOKABLE QVariantList getRunningWindows();

    // Font database helpers (for FontPickerDialog)
    Q_INVOKABLE QStringList fontStylesForFamily(const QString& family) const;
    Q_INVOKABLE int fontStyleWeight(const QString& family, const QString& style) const;
    Q_INVOKABLE bool fontStyleItalic(const QString& family, const QString& style) const;

    // Pywal integration
    Q_INVOKABLE void loadColorsFromPywal();
    Q_INVOKABLE void loadColorsFromFile(const QString& filePath);

    // Editor shortcuts reset
    Q_INVOKABLE void resetEditorShortcuts();

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
    // Per-monitor disable (no overlay, no zone picker, no snapping on that monitor)
    Q_INVOKABLE bool isMonitorDisabled(const QString& screenName) const;
    Q_INVOKABLE void setMonitorDisabled(const QString& screenName, bool disabled);

    // Per-screen zone selector settings
    Q_INVOKABLE QVariantMap getPerScreenZoneSelectorSettings(const QString& screenName) const;
    Q_INVOKABLE void setPerScreenZoneSelectorSetting(const QString& screenName, const QString& key, const QVariant& value);
    Q_INVOKABLE void clearPerScreenZoneSelectorSettings(const QString& screenName);
    Q_INVOKABLE bool hasPerScreenZoneSelectorSettings(const QString& screenName) const;
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
    // Daemon control
    Q_INVOKABLE void startDaemon();
    Q_INVOKABLE void stopDaemon();

    // Update checker
    Q_INVOKABLE void checkForUpdates();
    Q_INVOKABLE void openReleaseUrl();

Q_SIGNALS:
    void dragActivationTriggersChanged();
    void zoneSpanEnabledChanged();
    void zoneSpanTriggersChanged();
    void toggleActivationChanged();
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
    void labelFontColorChanged();
    void activeOpacityChanged();
    void inactiveOpacityChanged();
    void borderWidthChanged();
    void borderRadiusChanged();
    void enableBlurChanged();
    void labelFontFamilyChanged();
    void labelFontSizeScaleChanged();
    void labelFontWeightChanged();
    void labelFontItalicChanged();
    void labelFontUnderlineChanged();
    void labelFontStrikeoutChanged();
    void enableShaderEffectsChanged();
    void shaderFrameRateChanged();
    void enableAudioVisualizerChanged();
    void audioSpectrumBarCountChanged();
    void zonePaddingChanged();
    void outerGapChanged();
    void adjacentThresholdChanged();
    void keepWindowsInZonesOnResolutionChangeChanged();
    void moveNewWindowsToLastZoneChanged();
    void restoreOriginalSizeOnUnsnapChanged();
    void stickyWindowHandlingChanged();
    void restoreWindowsToZonesOnLoginChanged();
    void snapAssistFeatureEnabledChanged();
    void snapAssistEnabledChanged();
    void snapAssistTriggersChanged();
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
    void updateAvailableChanged();
    void latestVersionChanged();
    void releaseUrlChanged();
    void checkingForUpdatesChanged();
    void dismissedUpdateVersionChanged();
    void appRulesRefreshed(); // Emitted when app rules cache is cleared (load/defaults)
    void colorImportError(const QString& message); // Emitted when color import fails
    void colorImportSuccess(); // Emitted when color import succeeds

private Q_SLOTS:
    void loadLayouts();
    void refreshScreens();
    void checkDaemonStatus();
    void refreshVirtualDesktops();
    void refreshActivities();
    void onScreenLayoutChanged(const QString& screenName, const QString& layoutId);
    void onQuickLayoutSlotsChanged();
    void onSettingsChanged();
    void onCurrentActivityChanged(const QString& activityId);
    void onActivitiesChanged();

private:
    // Get screen name from the focused KCM window (for editor targeting on Wayland)
    QString currentScreenName() const;
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

    // Fire-and-forget async D-Bus call with error logging
    void watchAsyncDbusCall(QDBusPendingCall call, const QString& operation);

    /**
     * @brief Get Editor config group from plasmazonesrc
     * @return KConfigGroup for the Editor section
     */
    static KConfigGroup editorConfigGroup();

    // Trigger list conversion helpers (DragModifier enum <-> Qt bitmask)
    static QVariantList convertTriggersForQml(const QVariantList& triggers);
    static QVariantList convertTriggersForStorage(const QVariantList& triggers);

    Settings* m_settings = nullptr;
    UpdateChecker* m_updateChecker = nullptr;
    QString m_dismissedUpdateVersion;  // Cached to avoid repeated config reads
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

    // Pending layout visibility changes (staged until Apply)
    // Key: layoutId, Value: hiddenFromSelector state
    QHash<QString, bool> m_pendingHiddenStates;

    // Pending auto-assign changes (staged until Apply)
    // Key: layoutId, Value: autoAssign state
    QHash<QString, bool> m_pendingAutoAssignStates;

    // Pending app-to-zone rules (staged until Apply)
    // Key: layoutId, Value: rules list (QVariantList of {pattern, zoneNumber})
    // Only layouts that have been modified are stored here.
    QHash<QString, QVariantList> m_pendingAppRules;

    // Fill on drop settings (stored separately in Editor group)
    bool m_fillOnDropEnabled = true;
    int m_fillOnDropModifier = 0x04000000; // Qt::ControlModifier

    // Save guard to prevent re-entry during synchronous D-Bus operations
    bool m_saveInProgress = false;
};

} // namespace PlasmaZones
