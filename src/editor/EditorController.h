// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QVariantList>
#include <QRectF>
#include <QUuid>
#include <QScreen>
#include <KConfigGroup>
#include "../core/constants.h"
#include "../core/logging.h"
#include "undo/UndoController.h"

namespace PlasmaZones {

class Layout;
class ILayoutService;
class ZoneManager;
class SnappingService;
class TemplateService;

/**
 * @brief Controller for the layout editor
 *
 * Manages zone editing operations and communicates with the daemon via D-Bus.
 * Exposed to QML for the editor UI.
 */
class EditorController : public QObject
{
    Q_OBJECT

    // Layout properties
    Q_PROPERTY(QString layoutId READ layoutId NOTIFY layoutIdChanged)
    Q_PROPERTY(QString layoutName READ layoutName WRITE setLayoutName NOTIFY layoutNameChanged)
    Q_PROPERTY(QVariantList zones READ zones NOTIFY zonesChanged)
    // Lightweight version counter for efficient QML binding dependencies.
    // Use this instead of accessing 'zones' when you only need to detect changes.
    // Avoids copying the entire QVariantList just to create a binding dependency.
    Q_PROPERTY(int zonesVersion READ zonesVersion NOTIFY zonesChanged)
    Q_PROPERTY(QString selectedZoneId READ selectedZoneId WRITE setSelectedZoneId NOTIFY selectedZoneIdChanged)
    Q_PROPERTY(QStringList selectedZoneIds READ selectedZoneIds WRITE setSelectedZoneIds NOTIFY selectedZoneIdsChanged)
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY selectedZoneIdsChanged)
    Q_PROPERTY(bool hasMultipleSelection READ hasMultipleSelection NOTIFY selectedZoneIdsChanged)
    Q_PROPERTY(bool hasUnsavedChanges READ hasUnsavedChanges NOTIFY hasUnsavedChangesChanged)
    Q_PROPERTY(bool isNewLayout READ isNewLayout NOTIFY isNewLayoutChanged)

    // Snapping settings
    Q_PROPERTY(bool gridSnappingEnabled READ gridSnappingEnabled WRITE setGridSnappingEnabled NOTIFY
                   gridSnappingEnabledChanged)
    Q_PROPERTY(bool edgeSnappingEnabled READ edgeSnappingEnabled WRITE setEdgeSnappingEnabled NOTIFY
                   edgeSnappingEnabledChanged)
    Q_PROPERTY(qreal snapIntervalX READ snapIntervalX WRITE setSnapIntervalX NOTIFY snapIntervalXChanged)
    Q_PROPERTY(qreal snapIntervalY READ snapIntervalY WRITE setSnapIntervalY NOTIFY snapIntervalYChanged)
    Q_PROPERTY(
        qreal snapInterval READ snapInterval WRITE setSnapInterval NOTIFY snapIntervalChanged) // Backward compatibility
    Q_PROPERTY(
        bool gridOverlayVisible READ gridOverlayVisible WRITE setGridOverlayVisible NOTIFY gridOverlayVisibleChanged)

    // Keyboard shortcuts (app-specific configurable shortcuts only)
    // Standard shortcuts (Save, Delete, Close) use Qt StandardKey (system shortcuts)
    Q_PROPERTY(QString editorDuplicateShortcut READ editorDuplicateShortcut NOTIFY editorDuplicateShortcutChanged)
    Q_PROPERTY(QString editorSplitHorizontalShortcut READ editorSplitHorizontalShortcut NOTIFY
                   editorSplitHorizontalShortcutChanged)
    Q_PROPERTY(
        QString editorSplitVerticalShortcut READ editorSplitVerticalShortcut NOTIFY editorSplitVerticalShortcutChanged)
    Q_PROPERTY(QString editorFillShortcut READ editorFillShortcut NOTIFY editorFillShortcutChanged)
    Q_PROPERTY(int snapOverrideModifier READ snapOverrideModifier WRITE setSnapOverrideModifier NOTIFY
                   snapOverrideModifierChanged)

    // Fill on drop settings
    Q_PROPERTY(bool fillOnDropEnabled READ fillOnDropEnabled WRITE setFillOnDropEnabled NOTIFY fillOnDropEnabledChanged)
    Q_PROPERTY(
        int fillOnDropModifier READ fillOnDropModifier WRITE setFillOnDropModifier NOTIFY fillOnDropModifierChanged)

    // Screen
    Q_PROPERTY(QString targetScreen READ targetScreen WRITE setTargetScreen NOTIFY targetScreenChanged)

    // Zone settings (per-layout override or global settings)
    Q_PROPERTY(int zonePadding READ zonePadding WRITE setZonePadding NOTIFY zonePaddingChanged)
    Q_PROPERTY(int outerGap READ outerGap WRITE setOuterGap NOTIFY outerGapChanged)
    Q_PROPERTY(bool hasZonePaddingOverride READ hasZonePaddingOverride NOTIFY zonePaddingChanged)
    Q_PROPERTY(bool hasOuterGapOverride READ hasOuterGapOverride NOTIFY outerGapChanged)
    Q_PROPERTY(int globalZonePadding READ globalZonePadding NOTIFY globalZonePaddingChanged)
    Q_PROPERTY(int globalOuterGap READ globalOuterGap NOTIFY globalOuterGapChanged)

    // Shader properties for current layout
    Q_PROPERTY(QString currentShaderId READ currentShaderId WRITE setCurrentShaderId NOTIFY currentShaderIdChanged)
    Q_PROPERTY(QVariantMap currentShaderParams READ currentShaderParams WRITE setCurrentShaderParams NOTIFY
                   currentShaderParamsChanged)
    Q_PROPERTY(QVariantList availableShaders READ availableShaders NOTIFY availableShadersChanged)
    Q_PROPERTY(QVariantList currentShaderParameters READ currentShaderParameters NOTIFY currentShaderParametersChanged)
    Q_PROPERTY(bool shadersEnabled READ shadersEnabled NOTIFY shadersEnabledChanged)
    Q_PROPERTY(bool hasShaderEffect READ hasShaderEffect NOTIFY currentShaderIdChanged)
    Q_PROPERTY(QString noneShaderUuid READ noneShaderUuid CONSTANT)

    // Visibility filtering (Tier 2 per-context allow-lists)
    Q_PROPERTY(QStringList allowedScreens READ allowedScreens WRITE setAllowedScreens NOTIFY allowedScreensChanged)
    Q_PROPERTY(QVariantList allowedDesktops READ allowedDesktops WRITE setAllowedDesktops NOTIFY allowedDesktopsChanged)
    Q_PROPERTY(QStringList allowedActivities READ allowedActivities WRITE setAllowedActivities NOTIFY allowedActivitiesChanged)

    // Context info for visibility UI
    Q_PROPERTY(QStringList availableScreenNames READ availableScreenNames NOTIFY availableScreenNamesChanged)
    Q_PROPERTY(int virtualDesktopCount READ virtualDesktopCount NOTIFY virtualDesktopCountChanged)
    Q_PROPERTY(QStringList virtualDesktopNames READ virtualDesktopNames NOTIFY virtualDesktopNamesChanged)
    Q_PROPERTY(bool activitiesAvailable READ activitiesAvailable NOTIFY activitiesAvailableChanged)
    Q_PROPERTY(QVariantList availableActivities READ availableActivities NOTIFY availableActivitiesChanged)

    // Clipboard operations
    Q_PROPERTY(bool canPaste READ canPaste NOTIFY canPasteChanged)
    Q_PROPERTY(UndoController* undoController READ undoController CONSTANT)

public:
    explicit EditorController(QObject* parent = nullptr);
    ~EditorController() override;

    // Property getters
    QString layoutId() const;
    QString layoutName() const;
    QVariantList zones() const; // Delegates to ZoneManager
    int zonesVersion() const { return m_zonesVersion; } // Lightweight change counter
    QString selectedZoneId() const;
    QStringList selectedZoneIds() const;
    int selectionCount() const;
    bool hasMultipleSelection() const;
    bool hasUnsavedChanges() const;
    bool isNewLayout() const;
    bool gridSnappingEnabled() const;
    bool edgeSnappingEnabled() const;
    qreal snapIntervalX() const;
    qreal snapIntervalY() const;
    qreal snapInterval() const; // Backward compatibility
    bool gridOverlayVisible() const;
    QString editorDuplicateShortcut() const;
    QString editorSplitHorizontalShortcut() const;
    QString editorSplitVerticalShortcut() const;
    QString editorFillShortcut() const;
    int snapOverrideModifier() const;
    bool fillOnDropEnabled() const;
    int fillOnDropModifier() const;
    QString targetScreen() const;
    int zonePadding() const;
    int outerGap() const;
    bool hasZonePaddingOverride() const;
    bool hasOuterGapOverride() const;
    int globalZonePadding() const;
    int globalOuterGap() const;
    bool canPaste() const;
    UndoController* undoController() const;

    // Visibility filtering getters
    QStringList allowedScreens() const { return m_allowedScreens; }
    QVariantList allowedDesktops() const;
    QStringList allowedActivities() const { return m_allowedActivities; }
    QStringList availableScreenNames() const { return m_availableScreenNames; }
    int virtualDesktopCount() const { return m_virtualDesktopCount; }
    QStringList virtualDesktopNames() const { return m_virtualDesktopNames; }
    bool activitiesAvailable() const { return m_activitiesAvailable; }
    QVariantList availableActivities() const { return m_availableActivities; }

    // Visibility filtering setters
    void setAllowedScreens(const QStringList& screens);
    void setAllowedDesktops(const QVariantList& desktops);
    void setAllowedActivities(const QStringList& activities);

    // Shader getters
    QString currentShaderId() const;
    QVariantMap currentShaderParams() const;
    QVariantList availableShaders() const
    {
        return m_availableShaders;
    }
    QVariantList currentShaderParameters() const;
    bool shadersEnabled() const
    {
        return m_shadersEnabled;
    }
    bool hasShaderEffect() const;
    QString noneShaderUuid() const;

    // Property setters
    void setLayoutName(const QString& name);
    void setLayoutNameDirect(const QString& name); // For undo/redo (bypasses command creation)
    void setSelectedZoneId(const QString& zoneId);
    Q_INVOKABLE void setSelectedZoneIds(const QStringList& zoneIds);
    void setSelectedZoneIdsDirect(const QStringList& zoneIds); // For undo/redo (bypasses command creation)
    void setGridSnappingEnabled(bool enabled);
    void setEdgeSnappingEnabled(bool enabled);
    void setSnapIntervalX(qreal interval);
    void setSnapIntervalY(qreal interval);
    void setSnapInterval(qreal interval); // Backward compatibility: sets both X and Y
    void setGridOverlayVisible(bool visible);
    void setSnapOverrideModifier(int modifier);
    void setFillOnDropEnabled(bool enabled);
    void setFillOnDropModifier(int modifier);
    void setTargetScreen(const QString& screenName);
    void setTargetScreenDirect(const QString& screenName); // Sets screen without loading layout (for initialization)
    void setZonePadding(int padding);
    void setOuterGap(int gap);
    Q_INVOKABLE void clearZonePaddingOverride();
    Q_INVOKABLE void clearOuterGapOverride();
    Q_INVOKABLE void refreshGlobalZonePadding();
    Q_INVOKABLE void refreshGlobalOuterGap();

    // Shader setters (create undo commands)
    void setCurrentShaderId(const QString& id);
    void setCurrentShaderParams(const QVariantMap& params);

    // Shader setters - Direct (for undo/redo, bypass command creation)
    void setCurrentShaderIdDirect(const QString& id);
    void setCurrentShaderParamsDirect(const QVariantMap& params);
    void setShaderParameterDirect(const QString& key, const QVariant& value);

    // Visibility setters - Direct (for undo/redo, bypass command creation)
    void setAllowedScreensDirect(const QStringList& screens);
    void setAllowedDesktopsDirect(const QList<int>& desktops);
    void setAllowedActivitiesDirect(const QStringList& activities);

    // Shader operations (QML-invokable)
    Q_INVOKABLE void setShaderParameter(const QString& key, const QVariant& value);
    Q_INVOKABLE void resetShaderParameters();
    Q_INVOKABLE void switchShader(const QString& id, const QVariantMap& params);
    Q_INVOKABLE void refreshAvailableShaders();

public Q_SLOTS:
    // Layout operations
    void createNewLayout();
    void loadLayout(const QString& layoutId);
    void saveLayout();
    void discardChanges();

    // Zone CRUD operations (using zone IDs)
    QString addZone(qreal x, qreal y, qreal width, qreal height);
    void updateZoneGeometry(const QString& zoneId, qreal x, qreal y, qreal width, qreal height,
                            bool skipSnapping = false);
    void updateZoneName(const QString& zoneId, const QString& name);
    void updateZoneNumber(const QString& zoneId, int number);
    void updateZoneColor(const QString& zoneId, const QString& colorType, const QString& color);
    Q_INVOKABLE void updateZoneAppearance(const QString& zoneId, const QString& propertyName, const QVariant& value);
    void deleteZone(const QString& zoneId);
    QString duplicateZone(const QString& zoneId);

    // Zone splitting - split a zone horizontally or vertically
    Q_INVOKABLE QString splitZone(const QString& zoneId, bool horizontal);

    // Helper: get zone index by ID
    // Delegates to ZoneManager
    int zoneIndexById(const QString& zoneId) const;

    /**
     * @brief Get complete zone data by ID
     * @param zoneId Zone ID to retrieve
     * @return Complete zone data as QVariantMap, or empty map if not found
     *
     * Performance optimization: O(1) lookup instead of O(n) JavaScript loop in QML.
     * Delegates to ZoneManager::getZoneById().
     */
    Q_INVOKABLE QVariantMap getZoneById(const QString& zoneId) const;

    // Divider resizing - find zones that share an edge
    Q_INVOKABLE QVariantList getZonesSharingEdge(const QString& zoneId, qreal edgeX, qreal edgeY,
                                                 qreal threshold = 0.01);
    Q_INVOKABLE void resizeZonesAtDivider(const QString& zoneId1, const QString& zoneId2, qreal newDividerX,
                                          qreal newDividerY, bool isVertical);

    // Auto-fill operations
    Q_INVOKABLE QVariantMap findAdjacentZones(const QString& zoneId);
    Q_INVOKABLE bool expandToFillSpace(const QString& zoneId, qreal mouseX = -1, qreal mouseY = -1);
    Q_INVOKABLE QVariantMap calculateFillRegion(const QString& zoneId, qreal mouseX, qreal mouseY);
    Q_INVOKABLE void deleteZoneWithFill(const QString& zoneId, bool autoFill = true);

    // Z-order operations
    Q_INVOKABLE void bringToFront(const QString& zoneId);
    Q_INVOKABLE void sendToBack(const QString& zoneId);
    Q_INVOKABLE void bringForward(const QString& zoneId);
    Q_INVOKABLE void sendBackward(const QString& zoneId);

    // Template operations
    void applyTemplate(const QString& templateType, int columns = 2, int rows = 2);
    void clearAllZones();

    // Snapping (delegates to SnappingService)
    QVariantMap snapGeometry(qreal x, qreal y, qreal width, qreal height, const QString& excludeZoneId = QString());
    Q_INVOKABLE QVariantMap snapGeometrySelective(qreal x, qreal y, qreal width, qreal height,
                                                  const QString& excludeZoneId, bool snapLeft, bool snapRight,
                                                  bool snapTop, bool snapBottom);

    // Keyboard navigation
    Q_INVOKABLE QString selectNextZone();
    Q_INVOKABLE QString selectPreviousZone();
    Q_INVOKABLE bool moveSelectedZone(int direction, qreal step = 0.01); // 0=left, 1=right, 2=up, 3=down
    Q_INVOKABLE bool resizeSelectedZone(int direction, qreal step = 0.01); // 0=left, 1=right, 2=top, 3=bottom

    // Multi-selection manipulation
    Q_INVOKABLE void addToSelection(const QString& zoneId);
    Q_INVOKABLE void removeFromSelection(const QString& zoneId);
    Q_INVOKABLE void toggleSelection(const QString& zoneId);
    Q_INVOKABLE void selectRange(const QString& fromId, const QString& toId);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE bool isSelected(const QString& zoneId) const;
    
    /**
     * @brief Check if all selected zones have useCustomColors enabled
     * @return true if all selected zones use custom colors, false otherwise
     *
     * Performance optimization: O(n) C++ lookup instead of O(n*m) JavaScript nested loops.
     */
    Q_INVOKABLE bool allSelectedUseCustomColors() const;
    
    /**
     * @brief Select zones that intersect with a rectangle
     * @param x Rectangle X in relative coordinates (0.0-1.0)
     * @param y Rectangle Y in relative coordinates (0.0-1.0)
     * @param width Rectangle width in relative coordinates
     * @param height Rectangle height in relative coordinates
     * @param additive If true, add to existing selection; if false, replace
     * @return List of selected zone IDs
     *
     * Performance optimization: Avoids QVariantList copy and JavaScript iteration.
     * Used during marquee/rectangle selection drag operations.
     */
    Q_INVOKABLE QStringList selectZonesInRect(qreal x, qreal y, qreal width, qreal height, bool additive);

    // Batch operations for multi-selection
    Q_INVOKABLE void deleteSelectedZones();
    Q_INVOKABLE QStringList duplicateSelectedZones();
    Q_INVOKABLE bool moveSelectedZones(int direction, qreal step = 0.01);
    Q_INVOKABLE bool resizeSelectedZones(int direction, qreal step = 0.01);

    // Multi-zone drag operations
    /**
     * @brief Starts a multi-zone drag operation
     * @param primaryZoneId The zone being directly dragged
     * @param startX Initial X position of the primary zone
     * @param startY Initial Y position of the primary zone
     */
    Q_INVOKABLE void startMultiZoneDrag(const QString& primaryZoneId, qreal startX, qreal startY);

    /**
     * @brief Updates positions during multi-zone drag
     * @param primaryZoneId The zone being directly dragged
     * @param newX New X position of the primary zone
     * @param newY New Y position of the primary zone
     */
    Q_INVOKABLE void updateMultiZoneDrag(const QString& primaryZoneId, qreal newX, qreal newY);

    /**
     * @brief Ends the multi-zone drag operation
     * @param commit If true, commits the geometry changes; if false, cancels
     */
    Q_INVOKABLE void endMultiZoneDrag(bool commit);

    /**
     * @brief Checks if a multi-zone drag is in progress
     */
    Q_INVOKABLE bool isMultiZoneDragActive() const;

    // Batch appearance operations for multi-selection
    /**
     * @brief Updates an appearance property for all selected zones
     * @param propertyName Property to update (useCustomColors, opacity, borderWidth, borderRadius)
     * @param value New value for the property
     */
    Q_INVOKABLE void updateSelectedZonesAppearance(const QString& propertyName, const QVariant& value);

    /**
     * @brief Updates a color property for all selected zones
     * @param colorType Color to update (highlightColor, inactiveColor, borderColor)
     * @param color New color value (ARGB hex string)
     */
    Q_INVOKABLE void updateSelectedZonesColor(const QString& colorType, const QString& color);

    // Validation
    Q_INVOKABLE QString validateZoneName(const QString& zoneId, const QString& name);
    Q_INVOKABLE QString validateZoneNumber(const QString& zoneId, int number);

    // Default colors (for theme-based defaults)
    Q_INVOKABLE void setDefaultZoneColors(const QString& highlightColor, const QString& inactiveColor,
                                          const QString& borderColor);

    // Visibility filtering toggle methods
    Q_INVOKABLE void toggleScreenAllowed(const QString& screenName);
    Q_INVOKABLE void toggleDesktopAllowed(int desktop);
    Q_INVOKABLE void toggleActivityAllowed(const QString& activityId);

    // Import/Export operations
    Q_INVOKABLE void importLayout(const QString& filePath);
    Q_INVOKABLE void exportLayout(const QString& filePath);

    // Clipboard operations
    /**
     * @brief Copies selected zones to clipboard
     * @param zoneIds List of zone IDs to copy
     */
    Q_INVOKABLE void copyZones(const QStringList& zoneIds);

    /**
     * @brief Cuts selected zones (copy + delete)
     * @param zoneIds List of zone IDs to cut
     */
    Q_INVOKABLE void cutZones(const QStringList& zoneIds);

    /**
     * @brief Pastes zones from clipboard
     * @param withOffset If true, offset pasted zones by 2% to avoid overlap
     * @return List of newly pasted zone IDs, or empty list on failure
     */
    Q_INVOKABLE QStringList pasteZones(bool withOffset = false);

Q_SIGNALS:
    void layoutIdChanged();
    void layoutNameChanged();
    void zonesChanged();
    void selectedZoneIdChanged();
    void selectedZoneIdsChanged();
    void hasUnsavedChangesChanged();
    void isNewLayoutChanged();
    void gridSnappingEnabledChanged();
    void edgeSnappingEnabledChanged();
    void snapIntervalXChanged();
    void snapIntervalYChanged();
    void snapIntervalChanged(); // For backward compatibility
    void gridOverlayVisibleChanged();
    void editorDuplicateShortcutChanged();
    void editorSplitHorizontalShortcutChanged();
    void editorSplitVerticalShortcutChanged();
    void editorFillShortcutChanged();
    void snapOverrideModifierChanged();
    void fillOnDropEnabledChanged();
    void fillOnDropModifierChanged();
    void targetScreenChanged();
    void zonePaddingChanged();
    void outerGapChanged();
    void globalZonePaddingChanged();
    void globalOuterGapChanged();

    // Shader signals
    void currentShaderIdChanged();
    void currentShaderParamsChanged();
    void availableShadersChanged();
    void currentShaderParametersChanged();
    void shadersEnabledChanged();

    // Visibility filtering signals
    void allowedScreensChanged();
    void allowedDesktopsChanged();
    void allowedActivitiesChanged();
    void availableScreenNamesChanged();
    void virtualDesktopCountChanged();
    void virtualDesktopNamesChanged();
    void activitiesAvailableChanged();
    void availableActivitiesChanged();

    // Incremental update signals (to avoid full Repeater rebuilds)
    void zoneGeometryChanged(const QString& zoneId);
    void zoneNameChanged(const QString& zoneId);
    void zoneNumberChanged(const QString& zoneId);
    void zoneColorChanged(const QString& zoneId);
    void zoneAdded(const QString& zoneId);
    void zoneRemoved(const QString& zoneId);

    void layoutSaved();
    void layoutLoadFailed(const QString& error);
    void layoutSaveFailed(const QString& error);
    void editorClosed();

    // Validation signals
    void zoneNameValidationError(const QString& zoneId, const QString& error);
    void zoneNumberValidationError(const QString& zoneId, const QString& error);

    // Clipboard signals
    void canPasteChanged();
    void clipboardOperationFailed(const QString& error);

private:
    void markUnsaved();

    /**
     * @brief Remove shader params that don't belong to the current shader
     *
     * Uses m_cachedShaderParameters to determine valid param IDs.
     * Returns a new map containing only keys that match the current shader's definitions.
     */
    QVariantMap stripStaleShaderParams(const QVariantMap& params) const;

    /**
     * @brief Z-order operation types for changeZOrderImpl
     */
    enum class ZOrderOp { BringToFront, SendToBack, BringForward, SendBackward };

    /**
     * @brief Internal implementation for all z-order operations
     * @param zoneId Zone to modify
     * @param op Z-order operation to perform
     * @param actionName Undo action display name (already translated)
     */
    void changeZOrderImpl(const QString& zoneId, ZOrderOp op, const QString& actionName);

    /**
     * @brief Check if required services are ready for operations
     * @param operation Description of the operation (for logging)
     * @return true if both m_undoController and m_zoneManager are valid
     */
    bool servicesReady(const char* operation) const;

    /**
     * @brief Sync single-selection with multi-selection and emit signals
     *
     * Updates m_selectedZoneId to match first item in m_selectedZoneIds
     * and emits the appropriate changed signals. Call after modifying
     * m_selectedZoneIds directly.
     */
    void syncSelectionSignals();

    /**
     * @brief Load a shortcut from config with validation
     * @param group KConfig group to read from
     * @param key Config key name
     * @param defaultValue Default shortcut if not set or empty
     * @param member Reference to member variable to update
     * @param emitSignal Lambda to emit the changed signal
     */
    template<typename F>
    void loadShortcutSetting(KConfigGroup& group, const QString& key,
                             const QString& defaultValue, QString& member, F emitSignal)
    {
        QString value = group.readEntry(key, defaultValue);
        if (value.isEmpty()) {
            qCWarning(lcEditor) << "Invalid editor shortcut" << key << "(empty), using default";
            value = defaultValue;
        }
        if (member != value) {
            member = value;
            emitSignal();
        }
    }

    /**
     * @brief Loads editor settings from KConfig
     */
    void loadEditorSettings();

    /**
     * @brief Saves editor settings to KConfig
     */
    void saveEditorSettings();

    /**
     * @brief Handles clipboard content changes
     *
     * Updates canPaste property and emits signal when clipboard state changes.
     * This enables reactive QML bindings to update when clipboard changes.
     */
    void onClipboardChanged();

    /**
     * @brief Gets shader info from daemon via D-Bus
     * @param shaderId Shader ID to query
     * @return Shader metadata as QVariantMap, or empty map if not found
     */
    QVariantMap getShaderInfo(const QString& shaderId) const;

    // Layout data
    QString m_layoutId;
    QString m_layoutName;
    int m_zonesVersion = 0; // Increments on any zone change (lightweight binding dependency)
    QString m_selectedZoneId; // Use ID instead of index (backward compat: synced with first of m_selectedZoneIds)
    QStringList m_selectedZoneIds; // Multi-selection: list of selected zone IDs
    bool m_hasUnsavedChanges = false;
    bool m_isNewLayout = false;

    // Services (dependency injection)
    ILayoutService* m_layoutService = nullptr;
    ZoneManager* m_zoneManager = nullptr;
    SnappingService* m_snappingService = nullptr;
    TemplateService* m_templateService = nullptr;
    UndoController* m_undoController = nullptr;

    bool m_gridOverlayVisible = true; // Grid overlay visibility (independent of snapping)

    // Keyboard shortcuts (app-specific, loaded from settings)
    // Standard shortcuts (Save, Delete, Close) use Qt StandardKey (system shortcuts)
    QString m_editorDuplicateShortcut = QStringLiteral("Ctrl+D");
    QString m_editorSplitHorizontalShortcut = QStringLiteral("Ctrl+Shift+H");
    QString m_editorSplitVerticalShortcut =
        QStringLiteral("Ctrl+Alt+V"); // Note: Ctrl+Shift+V conflicts with Paste with Offset
    QString m_editorFillShortcut = QStringLiteral("Ctrl+Shift+F");
    int m_snapOverrideModifier = 0x02000000; // Qt::ShiftModifier

    // Fill on drop settings
    bool m_fillOnDropEnabled = true; // Enabled by default
    int m_fillOnDropModifier = 0x04000000; // Qt::ControlModifier (different from snap override)

    // Screen
    QString m_targetScreen;

    // Default colors (for theme-based defaults, set from QML)
    QString m_defaultHighlightColor;
    QString m_defaultInactiveColor;
    QString m_defaultBorderColor;

    // Zone settings (per-layout override, -1 = use global)
    int m_zonePadding = -1;
    int m_outerGap = -1;
    int m_cachedGlobalZonePadding = PlasmaZones::Defaults::ZonePadding; // Cached to avoid D-Bus calls
    int m_cachedGlobalOuterGap = PlasmaZones::Defaults::OuterGap; // Cached to avoid D-Bus calls

    // Clipboard state
    bool m_canPaste = false;

    // Shader state (cached from D-Bus query - NOT a local ShaderRegistry)
    QVariantList m_availableShaders;
    bool m_shadersEnabled = false;

    // Current layout's shader settings
    QString m_currentShaderId; // Empty = no shader effect
    QVariantMap m_currentShaderParams;

    // Cache for current shader's parameter definitions (avoids repeated D-Bus calls)
    // Updated when shader selection changes
    QVariantList m_cachedShaderParameters;

    // Visibility filtering state
    QStringList m_allowedScreens;
    QList<int> m_allowedDesktopsInt;
    QStringList m_allowedActivities;
    QStringList m_availableScreenNames;
    int m_virtualDesktopCount = 1;
    QStringList m_virtualDesktopNames;
    bool m_activitiesAvailable = false;
    QVariantList m_availableActivities;

    // Multi-zone drag state
    bool m_multiZoneDragActive = false;
    QString m_dragPrimaryZoneId;
    qreal m_dragStartX = 0.0;
    qreal m_dragStartY = 0.0;
    QMap<QString, QPointF> m_dragInitialPositions; // Initial positions of all selected zones
};

} // namespace PlasmaZones
