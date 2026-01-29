// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
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
 * @brief Keyboard modifier options for drag activation
 *
 * On Wayland, modifier detection may not work reliably because background daemons
 * can't query global keyboard state. If modifiers aren't detected, use AlwaysActive
 * as a workaround.
 */
enum class DragModifier {
    Disabled = 0, ///< Disabled - zone overlay never shows on drag
    Shift = 1, ///< Hold Shift while dragging
    Ctrl = 2, ///< Hold Ctrl while dragging
    Alt = 3, ///< Hold Alt while dragging
    Meta = 4, ///< Hold Meta/Super while dragging
    CtrlAlt = 5, ///< Hold Ctrl+Alt while dragging
    CtrlShift = 6, ///< Hold Ctrl+Shift while dragging
    AltShift = 7, ///< Hold Alt+Shift while dragging
    AlwaysActive = 8, ///< Always show zones on any drag (no modifier needed)
    AltMeta = 9, ///< Hold Alt+Meta while dragging
    CtrlAltMeta = 10 ///< Hold Ctrl+Alt+Meta while dragging
};

/**
 * @brief Position options for the zone selector bar
 * Values correspond to 3x3 grid cell indices:
 *   0=TopLeft,    1=Top,    2=TopRight
 *   3=Left,       4=Center, 5=Right
 *   6=BottomLeft, 7=Bottom, 8=BottomRight
 */
enum class ZoneSelectorPosition {
    TopLeft = 0,
    Top = 1,
    TopRight = 2,
    Left = 3,
    // Center = 4 is invalid
    Right = 5,
    BottomLeft = 6,
    Bottom = 7,
    BottomRight = 8
};

/**
 * @brief Layout mode options for the zone selector
 */
enum class ZoneSelectorLayoutMode {
    Grid = 0, ///< Grid layout with configurable columns
    Horizontal = 1, ///< Single row layout
    Vertical = 2 ///< Single column layout
};

/**
 * @brief Size mode options for the zone selector
 */
enum class ZoneSelectorSizeMode {
    Auto = 0, ///< Auto-calculate preview size from screen dimensions and layout count
    Manual = 1 ///< Use explicit previewWidth/previewHeight settings
};

/**
 * @brief Sticky window handling options
 */
enum class StickyWindowHandling {
    TreatAsNormal = 0, ///< Sticky windows follow per-desktop behavior
    RestoreOnly = 1, ///< Allow restore, disable auto-snap
    IgnoreAll = 2 ///< Disable restore and auto-snap
};

/**
 * @brief OSD style options for layout switch notifications
 */
enum class OsdStyle {
    None = 0, ///< No OSD shown on layout switch
    Text = 1, ///< Text-only Plasma OSD (layout name)
    Preview = 2 ///< Visual layout preview OSD (default)
};

/**
 * @brief Abstract interface for settings management
 *
 * Allows dependency inversion - components depend on this interface
 * rather than concrete Settings implementation.
 *
 * This class implements the Interface Segregation Principle by inheriting
 * from focused sub-interfaces. Components can depend on just the interfaces
 * they need (e.g., IZoneVisualizationSettings) rather than the full ISettings.
 *
 * Backward compatible - existing code using ISettings continues to work unchanged.
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

    // Persistence (unique to ISettings)
    virtual void load() = 0;
    virtual void save() = 0;
    virtual void reset() = 0;

Q_SIGNALS:
    void settingsChanged();
    void shiftDragToActivateChanged(); // Deprecated
    void dragActivationModifierChanged();
    void skipSnapModifierChanged();
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
    void zonePaddingChanged();
    void outerGapChanged();
    void adjacentThresholdChanged();
    void pollIntervalMsChanged();
    void minimumZoneSizePxChanged();
    void minimumZoneDisplaySizePxChanged();
    void keepWindowsInZonesOnResolutionChangeChanged();
    void moveNewWindowsToLastZoneChanged();
    void restoreOriginalSizeOnUnsnapChanged();
    void stickyWindowHandlingChanged();
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
    // Shader effects
    void enableShaderEffectsChanged();
    void shaderFrameRateChanged();
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

    // Keyboard Navigation Shortcuts (Phase 1 features)
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

    // Autotiling Settings
    void autotileEnabledChanged();
    void autotileAlgorithmChanged();
    void autotileSplitRatioChanged();
    void autotileMasterCountChanged();
    void autotileInnerGapChanged();
    void autotileOuterGapChanged();
    void autotileFocusNewWindowsChanged();
    void autotileSmartGapsChanged();
    void autotileInsertPositionChanged();

    // Autotiling Shortcuts
    void autotileToggleShortcutChanged();
    void autotileCycleAlgorithmShortcutChanged();
    void autotileFocusMasterShortcutChanged();
    void autotileSwapMasterShortcutChanged();
    void autotileIncMasterRatioShortcutChanged();
    void autotileDecMasterRatioShortcutChanged();
    void autotileIncMasterCountShortcutChanged();
    void autotileDecMasterCountShortcutChanged();
    void autotileRetileShortcutChanged();
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

    // Active layout
    virtual Layout* activeLayout() const = 0;
    virtual void setActiveLayout(Layout* layout) = 0;
    virtual void setActiveLayoutById(const QUuid& id) = 0;

    // Layout assignments
    virtual Layout* layoutForScreen(const QString& screenName, int virtualDesktop = 0,
                                    const QString& activity = QString()) const = 0;
    virtual void assignLayout(const QString& screenName, int virtualDesktop, const QString& activity,
                              Layout* layout) = 0;
    virtual void assignLayoutById(const QString& screenName, int virtualDesktop, const QString& activity,
                                  const QUuid& layoutId) = 0;
    virtual void clearAssignment(const QString& screenName, int virtualDesktop = 0,
                                 const QString& activity = QString()) = 0;
    virtual bool hasExplicitAssignment(const QString& screenName, int virtualDesktop = 0,
                                       const QString& activity = QString()) const = 0;

    // Quick layout switch
    virtual Layout* layoutForShortcut(int number) const = 0;
    virtual void applyQuickLayout(int number, const QString& screenName) = 0;
    virtual void setQuickLayoutSlot(int number, const QUuid& layoutId) = 0;
    virtual QHash<int, QUuid> quickLayoutSlots() const = 0;

    // Built-in layouts
    virtual void createBuiltInLayouts() = 0;
    virtual QVector<Layout*> builtInLayouts() const = 0;

    // Persistence
    virtual void loadLayouts(const QString& defaultLayoutId = QString()) = 0;
    virtual void saveLayouts() = 0;
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

    // Edge detection
    virtual QVector<Zone*> zonesNearEdge(const QPointF& point) const = 0;
    virtual bool isNearZoneEdge(const QPointF& point, Zone* zone) const = 0;
    virtual QRectF combineZoneGeometries(const QVector<Zone*>& zones) const = 0;

    // Highlight management
    virtual void highlightZone(Zone* zone) = 0;
    virtual void highlightZones(const QVector<Zone*>& zones) = 0;
    virtual void clearHighlights() = 0;

    // Detection thresholds
    virtual qreal adjacentThreshold() const = 0;
    virtual void setAdjacentThreshold(qreal threshold) = 0;
    virtual qreal edgeThreshold() const = 0;
    virtual void setEdgeThreshold(qreal threshold) = 0;

    // Multi-zone snapping
    virtual bool multiZoneEnabled() const = 0;
    virtual void setMultiZoneEnabled(bool enabled) = 0;

Q_SIGNALS:
    void layoutChanged();
    void zoneHighlighted(Zone* zone);
    void zonesHighlighted(const QVector<Zone*>& zones);
    void highlightsCleared();
    void adjacentThresholdChanged();
    void edgeThresholdChanged();
    void multiZoneEnabledChanged();
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
    virtual void showZoneSelector() = 0;
    virtual void hideZoneSelector() = 0;
    virtual void updateSelectorPosition(int cursorX, int cursorY) = 0;

    // Mouse position for shader effects (updated during window drag)
    virtual void updateMousePosition(int cursorX, int cursorY) = 0;

    // Zone selector selection tracking
    virtual bool hasSelectedZone() const = 0;
    virtual QString selectedLayoutId() const = 0;
    virtual int selectedZoneIndex() const = 0;
    virtual QRect getSelectedZoneGeometry(QScreen* screen) const = 0;
    virtual void clearSelectedZone() = 0;

Q_SIGNALS:
    void visibilityChanged(bool visible);
    void zoneActivated(Zone* zone);
    void multiZoneActivated(const QVector<Zone*>& zones);
    void zoneSelectorVisibilityChanged(bool visible);
    void zoneSelectorZoneSelected(int zoneIndex);
};

} // namespace PlasmaZones

Q_DECLARE_METATYPE(PlasmaZones::ZoneDetectionResult)
