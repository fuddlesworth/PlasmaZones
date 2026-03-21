// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include "enums.h"
#include "settings_interfaces.h"
#include <QObject>
#include <QColor>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <QVector>
#include <QRectF>
#include <QPointF>
#include <QRect>

class QScreen;

namespace PlasmaZones {

class Zone;
class Layout;

/**
 * @brief Abstract interface for settings management
 *
 * Allows dependency inversion - components depend on this interface
 * rather than concrete Settings implementation. Inherits from focused
 * sub-interfaces so components can depend on just what they need.
 */
class PLASMAZONES_EXPORT ISettings : public QObject,
                                     public IZoneActivationSettings,
                                     public IZoneVisualizationSettings,
                                     public IZoneGeometrySettings,
                                     public IWindowExclusionSettings,
                                     public IZoneSelectorSettings,
                                     public IWindowBehaviorSettings,
                                     public IDefaultLayoutSettings
{
    Q_OBJECT

public:
    explicit ISettings(QObject* parent = nullptr)
        : QObject(parent)
    {
    }
    ~ISettings() override;

    // ═══════════════════════════════════════════════════════════════════════════
    // All settings methods are inherited from the segregated interfaces:
    //   - IZoneActivationSettings: drag modifiers, activation triggers
    //   - IZoneVisualizationSettings: colors, opacity, blur, shader effects
    //   - IZoneGeometrySettings: padding, gaps, thresholds, performance
    //   - IWindowExclusionSettings: excluded apps/classes, size filters
    //   - IZoneSelectorSettings: zone selector UI configuration
    //   - IWindowBehaviorSettings: snap restore, sticky handling
    //   - IDefaultLayoutSettings: default layout ID
    //
    // See settings_interfaces.h for the full API.
    // ═══════════════════════════════════════════════════════════════════════════

    // Animation settings (global — applies to snapping and autotiling)
    virtual bool animationsEnabled() const = 0;
    virtual void setAnimationsEnabled(bool enabled) = 0;
    virtual int animationDuration() const = 0;
    virtual void setAnimationDuration(int duration) = 0;
    virtual QString animationEasingCurve() const = 0;
    virtual void setAnimationEasingCurve(const QString& curve) = 0;
    virtual int animationMinDistance() const = 0;
    virtual void setAnimationMinDistance(int distance) = 0;
    virtual int animationSequenceMode() const = 0;
    virtual void setAnimationSequenceMode(int mode) = 0;
    virtual int animationStaggerInterval() const = 0;
    virtual void setAnimationStaggerInterval(int ms) = 0;

    // Autotile decoration settings (fetched by KWin effect via D-Bus)
    virtual bool autotileFocusFollowsMouse() const = 0;
    virtual void setAutotileFocusFollowsMouse(bool enabled) = 0;
    virtual bool autotileHideTitleBars() const = 0;
    virtual void setAutotileHideTitleBars(bool hide) = 0;
    virtual bool autotileShowBorder() const = 0;
    virtual void setAutotileShowBorder(bool show) = 0;
    virtual int autotileBorderWidth() const = 0;
    virtual void setAutotileBorderWidth(int width) = 0;
    virtual int autotileBorderRadius() const = 0;
    virtual void setAutotileBorderRadius(int radius) = 0;
    virtual QColor autotileBorderColor() const = 0;
    virtual void setAutotileBorderColor(const QColor& color) = 0;
    virtual QColor autotileInactiveBorderColor() const = 0;
    virtual void setAutotileInactiveBorderColor(const QColor& color) = 0;
    virtual bool autotileUseSystemBorderColors() const = 0;
    virtual void setAutotileUseSystemBorderColors(bool use) = 0;

    // Persistence (unique to ISettings)
    virtual void load() = 0;
    virtual void save() = 0;
    virtual void reset() = 0;

Q_SIGNALS:
    void settingsChanged();
    void shiftDragToActivateChanged(); // Deprecated
    void dragActivationTriggersChanged();
    void zoneSpanEnabledChanged();
    void zoneSpanModifierChanged();
    void zoneSpanTriggersChanged();
    void toggleActivationChanged();
    void snappingEnabledChanged();
    void showZonesOnAllMonitorsChanged();
    void disabledMonitorsChanged();
    void showZoneNumbersChanged();
    void flashZonesOnSwitchChanged();
    void showOsdOnLayoutSwitchChanged();
    void showNavigationOsdChanged();
    void osdStyleChanged();
    void overlayDisplayModeChanged();
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
    void zonePaddingChanged();
    void outerGapChanged();
    void usePerSideOuterGapChanged();
    void outerGapTopChanged();
    void outerGapBottomChanged();
    void outerGapLeftChanged();
    void outerGapRightChanged();
    void adjacentThresholdChanged();
    void pollIntervalMsChanged();
    void minimumZoneSizePxChanged();
    void minimumZoneDisplaySizePxChanged();
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
    void perScreenZoneSelectorSettingsChanged();
    void perScreenAutotileSettingsChanged();
    void perScreenSnappingSettingsChanged();
    // Shader effects
    void enableShaderEffectsChanged();
    void shaderFrameRateChanged();
    void enableAudioVisualizerChanged();
    void audioSpectrumBarCountChanged();
    // Global shortcuts
    void openEditorShortcutChanged();
    void previousLayoutShortcutChanged();
    void nextLayoutShortcutChanged();
    void quickLayout1ShortcutChanged();
    void quickLayout2ShortcutChanged();
    void quickLayout3ShortcutChanged();
    void quickLayout4ShortcutChanged();
    void quickLayout5ShortcutChanged();
    void quickLayout6ShortcutChanged();
    void quickLayout7ShortcutChanged();
    void quickLayout8ShortcutChanged();
    void quickLayout9ShortcutChanged();

    // Keyboard Navigation Shortcuts
    void moveWindowLeftShortcutChanged();
    void moveWindowRightShortcutChanged();
    void moveWindowUpShortcutChanged();
    void moveWindowDownShortcutChanged();
    void focusZoneLeftShortcutChanged();
    void focusZoneRightShortcutChanged();
    void focusZoneUpShortcutChanged();
    void focusZoneDownShortcutChanged();
    void pushToEmptyZoneShortcutChanged();
    void restoreWindowSizeShortcutChanged();
    void toggleWindowFloatShortcutChanged();

    // Swap Window Shortcuts
    void swapWindowLeftShortcutChanged();
    void swapWindowRightShortcutChanged();
    void swapWindowUpShortcutChanged();
    void swapWindowDownShortcutChanged();

    // Snap to Zone by Number Shortcuts
    void snapToZone1ShortcutChanged();
    void snapToZone2ShortcutChanged();
    void snapToZone3ShortcutChanged();
    void snapToZone4ShortcutChanged();
    void snapToZone5ShortcutChanged();
    void snapToZone6ShortcutChanged();
    void snapToZone7ShortcutChanged();
    void snapToZone8ShortcutChanged();
    void snapToZone9ShortcutChanged();

    // Rotate Windows Shortcuts
    void rotateWindowsClockwiseShortcutChanged();
    void rotateWindowsCounterclockwiseShortcutChanged();

    // Cycle Windows in Zone Shortcuts
    void cycleWindowForwardShortcutChanged();
    void cycleWindowBackwardShortcutChanged();

    // Resnap to New Layout Shortcut
    void resnapToNewLayoutShortcutChanged();

    // Snap All Windows Shortcut
    void snapAllWindowsShortcutChanged();

    // Layout Picker Shortcut
    void layoutPickerShortcutChanged();

    // Toggle Layout Lock Shortcut
    void toggleLayoutLockShortcutChanged();

    // Autotile settings
    void autotileEnabledChanged();
    void autotileAlgorithmChanged();
    void autotileSplitRatioChanged();
    void autotileMasterCountChanged();
    void autotileCenteredMasterSplitRatioChanged();
    void autotileCenteredMasterMasterCountChanged();
    void autotileInnerGapChanged();
    void autotileOuterGapChanged();
    void autotileUsePerSideOuterGapChanged();
    void autotileOuterGapTopChanged();
    void autotileOuterGapBottomChanged();
    void autotileOuterGapLeftChanged();
    void autotileOuterGapRightChanged();
    void autotileSmartGapsChanged();
    void autotileMaxWindowsChanged();
    void autotileFocusNewWindowsChanged();
    void autotileInsertPositionChanged();
    void autotileRespectMinimumSizeChanged();
    void autotileFocusFollowsMouseChanged();
    void autotileHideTitleBarsChanged();
    void autotileShowBorderChanged();
    void autotileBorderWidthChanged();
    void autotileBorderRadiusChanged();
    void autotileBorderColorChanged();
    void autotileInactiveBorderColorChanged();
    void autotileUseSystemBorderColorsChanged();
    void lockedScreensChanged();
    // Animation settings (general)
    void animationsEnabledChanged();
    void animationDurationChanged();
    void animationEasingCurveChanged();
    void animationMinDistanceChanged();
    void animationSequenceModeChanged();
    void animationStaggerIntervalChanged();

    // Autotile shortcuts
    void autotileToggleShortcutChanged();
    void autotileRetileShortcutChanged();
    void autotileFocusMasterShortcutChanged();
    void autotileSwapMasterShortcutChanged();
    void autotileIncMasterCountShortcutChanged();
    void autotileDecMasterCountShortcutChanged();
    void autotileIncMasterRatioShortcutChanged();
    void autotileDecMasterRatioShortcutChanged();
};

/**
 * @brief Abstract interface for layout management
 *
 * This is a pure abstract interface (no QObject) defining the layout manager contract.
 * The concrete LayoutManager class inherits from QObject and provides the signals.
 *
 * Design rationale: Qt's signal system doesn't work well with abstract interfaces
 * because signal shadowing between base and derived classes causes heap corruption
 * when using new-style Qt::connect with function pointers. By making this interface
 * pure virtual (no QObject), we avoid the shadowing problem entirely.
 *
 * Components needing signals should use LayoutManager* directly.
 */
class PLASMAZONES_EXPORT ILayoutManager
{
public:
    ILayoutManager() = default;
    virtual ~ILayoutManager();

    // Layout directory
    virtual QString layoutDirectory() const = 0;
    virtual void setLayoutDirectory(const QString& directory) = 0;

    // Layout management
    virtual int layoutCount() const = 0;
    virtual QVector<Layout*> layouts() const = 0;
    virtual Layout* layout(int index) const = 0;
    virtual Layout* layoutById(const QUuid& id) const = 0;
    virtual Layout* layoutByName(const QString& name) const = 0;

    virtual void addLayout(Layout* layout) = 0;
    virtual void removeLayout(Layout* layout) = 0;
    virtual void removeLayoutById(const QUuid& id) = 0;
    virtual Layout* duplicateLayout(Layout* source) = 0;

    // Active layout (internal — used for resnap/geometry/overlay machinery)
    virtual Layout* activeLayout() const = 0;
    virtual void setActiveLayout(Layout* layout) = 0;
    virtual void setActiveLayoutById(const QUuid& id) = 0;

    // Default layout (settings-based fallback for the layout cascade)
    virtual Layout* defaultLayout() const = 0;

    // Current context for per-screen layout lookups
    virtual int currentVirtualDesktop() const = 0;
    virtual QString currentActivity() const = 0;

    /**
     * @brief Convenience: resolve layout for screen using current desktop/activity context
     *
     * Equivalent to layoutForScreen(screenId, currentVirtualDesktop(), currentActivity())
     * with a fallback to defaultLayout() when no per-screen assignment matches.
     * Use this everywhere a "give me the layout for this screen right now" is needed.
     *
     * @param screenId Stable EDID-based screen identifier (or connector name — auto-resolved)
     */
    Layout* resolveLayoutForScreen(const QString& screenId) const
    {
        Layout* layout = layoutForScreen(screenId, currentVirtualDesktop(), currentActivity());
        return layout ? layout : defaultLayout();
    }

    // Layout assignments (screenId: stable EDID-based identifier or connector name fallback)
    virtual Layout* layoutForScreen(const QString& screenId, int virtualDesktop = 0,
                                    const QString& activity = QString()) const = 0;
    virtual void assignLayout(const QString& screenId, int virtualDesktop, const QString& activity, Layout* layout) = 0;
    virtual void assignLayoutById(const QString& screenId, int virtualDesktop, const QString& activity,
                                  const QString& layoutId) = 0;
    virtual void clearAssignment(const QString& screenId, int virtualDesktop = 0,
                                 const QString& activity = QString()) = 0;
    virtual bool hasExplicitAssignment(const QString& screenId, int virtualDesktop = 0,
                                       const QString& activity = QString()) const = 0;
    virtual void setAllScreenAssignments(const QHash<QString, QString>& assignments) = 0; // Batch set - saves once
    virtual void
    setAllDesktopAssignments(const QHash<QPair<QString, int>, QString>& assignments) = 0; // Batch per-desktop
    virtual void
    setAllActivityAssignments(const QHash<QPair<QString, QString>, QString>& assignments) = 0; // Batch per-activity

    // Quick layout switch
    virtual Layout* layoutForShortcut(int number) const = 0;
    virtual void applyQuickLayout(int number, const QString& screenId) = 0;
    virtual void setQuickLayoutSlot(int number, const QString& layoutId) = 0;
    virtual void setAllQuickLayoutSlots(const QHash<int, QString>& slots) = 0; // Batch set - saves once
    virtual QHash<int, QString> quickLayoutSlots() const = 0;

    // Built-in layouts
    virtual void createBuiltInLayouts() = 0;
    virtual QVector<Layout*> builtInLayouts() const = 0;

    // Persistence
    virtual void loadLayouts() = 0;
    virtual void saveLayouts() = 0;
    virtual void saveLayout(Layout* layout) = 0;
    virtual void loadAssignments() = 0;
    virtual void saveAssignments() = 0;
    virtual void importLayout(const QString& filePath) = 0;
    virtual void exportLayout(Layout* layout, const QString& filePath) = 0;
};

/**
 * @brief Result of zone detection
 *
 * Defined here so interfaces can use it without circular dependencies.
 */
struct PLASMAZONES_EXPORT ZoneDetectionResult
{
    Zone* primaryZone = nullptr; // Main zone to snap to
    QVector<Zone*> adjacentZones; // Adjacent zones for multi-zone snap
    QVector<Zone*> overlappingZones; // All zones containing cursor point (overlap info)
    QRectF snapGeometry; // Combined geometry for snapping
    qreal distance = -1; // Distance to zone edge
    bool isMultiZone = false; // Whether snapping to multiple zones
};

/**
 * @brief Abstract interface for zone detection
 */
class PLASMAZONES_EXPORT IZoneDetector : public QObject
{
    Q_OBJECT

public:
    explicit IZoneDetector(QObject* parent = nullptr)
        : QObject(parent)
    {
    }
    ~IZoneDetector() override;

    virtual Layout* layout() const = 0;
    virtual void setLayout(Layout* layout) = 0;

    // Zone detection
    virtual ZoneDetectionResult detectZone(const QPointF& cursorPos) const = 0;
    virtual ZoneDetectionResult detectMultiZone(const QPointF& cursorPos) const = 0;
    virtual Zone* zoneAtPoint(const QPointF& point) const = 0;
    virtual Zone* nearestZone(const QPointF& point) const = 0;

    // Paint-to-snap: expand painted zones to include all zones intersecting the bounding rect
    // (same raycasting algorithm as detectMultiZone and the editor)
    virtual QVector<Zone*> expandPaintedZonesToRect(const QVector<Zone*>& seedZones) const = 0;

    // Highlight management
    virtual void highlightZone(Zone* zone) = 0;
    virtual void highlightZones(const QVector<Zone*>& zones) = 0;
    virtual void clearHighlights() = 0;

Q_SIGNALS:
    void layoutChanged();
    void zoneHighlighted(Zone* zone);
    void highlightsCleared();
};

/**
 * @brief Abstract interface for overlay management
 *
 * Separates UI concerns from the daemon.
 */
class PLASMAZONES_EXPORT IOverlayService : public QObject
{
    Q_OBJECT

public:
    explicit IOverlayService(QObject* parent = nullptr)
        : QObject(parent)
    {
    }
    ~IOverlayService() override;

    virtual bool isVisible() const = 0;
    virtual void show() = 0;
    virtual void showAtPosition(int cursorX, int cursorY) = 0; // For Wayland - uses cursor coords from KWin
    virtual void hide() = 0;
    virtual void toggle() = 0;

    virtual void updateLayout(Layout* layout) = 0;
    virtual void updateSettings(ISettings* settings) = 0;
    virtual void updateGeometries() = 0;

    // Zone highlighting for overlay display
    virtual void highlightZone(const QString& zoneId) = 0;
    virtual void highlightZones(const QStringList& zoneIds) = 0;
    virtual void clearHighlight() = 0;

    // Zone selector methods
    virtual bool isZoneSelectorVisible() const = 0;
    virtual void showZoneSelector(QScreen* screen = nullptr) = 0;
    virtual void hideZoneSelector() = 0;
    virtual void updateSelectorPosition(int cursorX, int cursorY) = 0;

    // Mouse position for shader effects (updated during window drag)
    virtual void updateMousePosition(int cursorX, int cursorY) = 0;

    // Filtered layout count (matches what the zone selector actually displays)
    virtual int visibleLayoutCount(const QString& screenId) const = 0;

    // Zone selector selection tracking
    virtual bool hasSelectedZone() const = 0;
    virtual QString selectedLayoutId() const = 0;
    virtual int selectedZoneIndex() const = 0;
    virtual QRect getSelectedZoneGeometry(QScreen* screen) const = 0;
    virtual void clearSelectedZone() = 0;

    // Shader preview overlay (editor dialog - dedicated window avoids multi-pass clear)
    virtual void showShaderPreview(int x, int y, int width, int height, const QString& screenId,
                                   const QString& shaderId, const QString& shaderParamsJson,
                                   const QString& zonesJson) = 0;
    virtual void updateShaderPreview(int x, int y, int width, int height, const QString& shaderParamsJson,
                                     const QString& zonesJson) = 0;
    virtual void hideShaderPreview() = 0;

    // Snap Assist overlay (window picker after snapping)
    virtual void showSnapAssist(const QString& screenId, const QString& emptyZonesJson,
                                const QString& candidatesJson) = 0;
    virtual void hideSnapAssist() = 0;
    virtual bool isSnapAssistVisible() const = 0;
    virtual void setSnapAssistThumbnail(const QString& kwinHandle, const QString& dataUrl) = 0;

Q_SIGNALS:
    void visibilityChanged(bool visible);
    void zoneActivated(Zone* zone);
    void multiZoneActivated(const QVector<Zone*>& zones);
    void zoneSelectorVisibilityChanged(bool visible);
    void zoneSelectorZoneSelected(int zoneIndex);

    /**
     * @brief Emitted when a manual layout is selected from the zone selector
     * @param layoutId The UUID of the selected manual layout
     * @param screenId The screen where the selection was made
     */
    void manualLayoutSelected(const QString& layoutId, const QString& screenId);

    /**
     * @brief Emitted when user selects a window from Snap Assist to snap to a zone
     * @param windowId Window identifier to snap
     * @param zoneId Target zone UUID
     * @param geometryJson JSON {x, y, width, height} - used for display only; daemon fetches authoritative geometry
     * @param screenId Screen where the zone is for geometry lookup
     */
    void snapAssistWindowSelected(const QString& windowId, const QString& zoneId, const QString& geometryJson,
                                  const QString& screenId);

    /**
     * @brief Emitted when Snap Assist overlay is shown. KWin script subscribes to create thumbnails.
     */
    void snapAssistShown(const QString& screenId, const QString& emptyZonesJson, const QString& candidatesJson);

    /**
     * @brief Emitted when Snap Assist overlay is dismissed (by selection, Escape, or any other means).
     * WindowDragAdaptor subscribes to unregister the KGlobalAccel Escape shortcut.
     */
    void snapAssistDismissed();

    /**
     * @brief Emitted when a layout is selected from the layout picker overlay
     * @param layoutId The UUID of the selected layout
     */
    void layoutPickerSelected(const QString& layoutId);

    /**
     * @brief Emitted when an autotile algorithm layout is selected from the zone selector
     * @param algorithmId The algorithm identifier (e.g. "master-stack")
     * @param screenId The screen where the selection was made
     */
    void autotileLayoutSelected(const QString& algorithmId, const QString& screenId);
};

} // namespace PlasmaZones

Q_DECLARE_METATYPE(PlasmaZones::ZoneDetectionResult)
