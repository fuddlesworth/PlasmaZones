// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QVariantList>
#include <QRectF>
#include <QUuid>
#include <QScreen>
#include "../core/constants.h"
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

    // Zone settings (from global settings)
    Q_PROPERTY(int zonePadding READ zonePadding NOTIFY zonePaddingChanged)

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
    bool canPaste() const;
    UndoController* undoController() const;

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
     * @brief Serializes zones to JSON format for clipboard
     * @param zones List of zones to serialize
     * @return JSON string containing zone data
     */
    QString serializeZonesToClipboard(const QVariantList& zones);

    /**
     * @brief Deserializes zones from clipboard JSON format
     * @param clipboardText JSON string from clipboard
     * @return List of zones, or empty list if invalid data
     */
    QVariantList deserializeZonesFromClipboard(const QString& clipboardText);

    // Layout data
    QString m_layoutId;
    QString m_layoutName;
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

    // Snapping settings (delegated to SnappingService, cached for fallback)
    bool m_gridSnappingEnabled = true;
    bool m_edgeSnappingEnabled = true;
    qreal m_snapIntervalX = PlasmaZones::EditorConstants::DefaultSnapInterval;
    qreal m_snapIntervalY = PlasmaZones::EditorConstants::DefaultSnapInterval;
    qreal m_snapInterval = PlasmaZones::EditorConstants::DefaultSnapInterval; // For backward compatibility
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

    // Zone settings (from global settings)
    int m_zonePadding = PlasmaZones::Defaults::ZonePadding;

    // Clipboard state
    bool m_canPaste = false;

    // Multi-zone drag state
    bool m_multiZoneDragActive = false;
    QString m_dragPrimaryZoneId;
    qreal m_dragStartX = 0.0;
    qreal m_dragStartY = 0.0;
    QMap<QString, QPointF> m_dragInitialPositions; // Initial positions of all selected zones
};

} // namespace PlasmaZones
