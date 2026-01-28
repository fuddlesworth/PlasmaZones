// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QPair>
#include <QRectF>
#include <QSet>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

namespace PlasmaZones {

/**
 * @brief Manages zone CRUD operations
 *
 * Handles zone creation, updates, deletion, duplication, and splitting.
 * Maintains the zones list and emits signals for zone changes.
 */
class ZoneManager : public QObject
{
    Q_OBJECT

public:
    explicit ZoneManager(QObject* parent = nullptr);
    ~ZoneManager() override = default;

    // Zone CRUD operations
    QString addZone(qreal x, qreal y, qreal width, qreal height);
    void updateZoneGeometry(const QString& zoneId, qreal x, qreal y, qreal width, qreal height);
    /**
     * @brief Updates zone geometry without emitting signals (for multi-zone drag preview)
     */
    void updateZoneGeometryDirect(const QString& zoneId, qreal x, qreal y, qreal width, qreal height);
    void updateZoneName(const QString& zoneId, const QString& name);
    void updateZoneNumber(const QString& zoneId, int number);
    void updateZoneColor(const QString& zoneId, const QString& colorType, const QString& color);
    void updateZoneAppearance(const QString& zoneId, const QString& propertyName, const QVariant& value);
    void deleteZone(const QString& zoneId);
    QString duplicateZone(const QString& zoneId);
    QString splitZone(const QString& zoneId, bool horizontal);

    // Divider operations
    QVariantList getZonesSharingEdge(const QString& zoneId, qreal edgeX, qreal edgeY, qreal threshold = 0.01);
    void resizeZonesAtDivider(const QString& zoneId1, const QString& zoneId2, qreal newDividerX, qreal newDividerY,
                              bool isVertical);
    /**
     * @brief Collect (zoneId, geometry) for all zones that would be affected by a divider resize.
     * Used by DividerResizeCommand to capture state before resize for undo.
     */
    QVector<QPair<QString, QRectF>> collectGeometriesAtDivider(const QString& zoneId1, const QString& zoneId2,
                                                               bool isVertical);

    // Auto-fill operations
    /**
     * @brief Find zones adjacent to the given zone
     * @param zoneId The zone to find neighbors for
     * @param threshold Edge matching threshold (default 0.02 = 2%)
     * @return Map with "left", "right", "top", "bottom" lists of adjacent zone IDs
     */
    QVariantMap findAdjacentZones(const QString& zoneId, qreal threshold = 0.02);

    /**
     * @brief Expand a zone to fill available empty space around it
     * @param zoneId The zone to expand
     * @param mouseX Normalized mouse X position (0-1), or -1 to use zone center
     * @param mouseY Normalized mouse Y position (0-1), or -1 to use zone center
     * @return true if any expansion occurred
     */
    bool expandToFillSpace(const QString& zoneId, qreal mouseX = -1, qreal mouseY = -1);

    /**
     * @brief Calculate the fill region without applying it (for live preview)
     * @param zoneId The zone to calculate fill for (excluded from collision checks)
     * @param mouseX Normalized mouse X position (0-1)
     * @param mouseY Normalized mouse Y position (0-1)
     * @return QVariantMap with x, y, width, height of fill region, or empty if no valid region
     */
    QVariantMap calculateFillRegion(const QString& zoneId, qreal mouseX, qreal mouseY);

    /**
     * @brief Delete a zone and optionally expand neighbors to fill the gap
     * @param zoneId The zone to delete
     * @param autoFill If true, expand adjacent zones to fill the deleted zone's space
     */
    void deleteZoneWithFill(const QString& zoneId, bool autoFill = true);

    // Z-order operations
    void bringToFront(const QString& zoneId);
    void sendToBack(const QString& zoneId);
    void bringForward(const QString& zoneId);
    void sendBackward(const QString& zoneId);

    // Bulk operations
    void clearAllZones();
    void setZones(const QVariantList& zones);
    QVariantList zones() const;

    // Batch update support (defers signal emission until batch completes)
    void beginBatchUpdate();
    void endBatchUpdate();

    // Default colors (can be set from QML to use theme colors)
    Q_INVOKABLE void setDefaultColors(const QString& highlightColor, const QString& inactiveColor,
                                      const QString& borderColor);

    // Helpers
    int findZoneIndex(const QString& zoneId) const;
    int zoneCount() const
    {
        return m_zones.size();
    }

    /**
     * @brief Adds a zone from complete QVariantMap (for paste operations)
     * @param zoneData Complete zone data including all properties (colors, appearance, etc.)
     * @param allowIdReuse If true, allows reusing existing zone IDs (for undo/redo operations)
     * @return Zone ID of the created zone, or empty string on failure
     *
     * Allows pasting zones with all their properties intact. Validates
     * zone data and creates new zone with specified properties.
     * If allowIdReuse is true and zone ID already exists, deletes existing zone first.
     */
    QString addZoneFromMap(const QVariantMap& zoneData, bool allowIdReuse = false);

    /**
     * @brief Get complete zone data by ID (for undo state and QML lookup)
     * @param zoneId Zone ID to retrieve
     * @return Complete zone data as QVariantMap, or empty map if not found
     *
     * This method is Q_INVOKABLE for efficient O(1) lookup from QML,
     * avoiding expensive O(n) loops in property bindings.
     */
    Q_INVOKABLE QVariantMap getZoneById(const QString& zoneId) const;

    /**
     * @brief Set complete zone data (for undo restoration)
     * @param zoneId Zone ID to update
     * @param zoneData Complete zone data including all properties
     */
    void setZoneData(const QString& zoneId, const QVariantMap& zoneData);

    /**
     * @brief Restore multiple zones (for template/layout operations)
     * @param zones List of complete zone data to restore
     */
    void restoreZones(const QVariantList& zones);

Q_SIGNALS:
    // Incremental update signals
    void zoneGeometryChanged(const QString& zoneId);
    void zoneNameChanged(const QString& zoneId);
    void zoneNumberChanged(const QString& zoneId);
    void zoneColorChanged(const QString& zoneId);
    void zoneZOrderChanged(const QString& zoneId);
    void zoneAdded(const QString& zoneId);
    void zoneRemoved(const QString& zoneId);
    void zonesChanged();

    void zonesModified(); // Generic signal for any zone modification

private:
    /**
     * @brief Creates a new zone map with default values
     */
    QVariantMap createZone(const QString& name, int number, qreal x, qreal y, qreal width, qreal height);

    /**
     * @brief Renumbers all zones sequentially starting from 1
     */
    void renumberZones();

    /**
     * @brief Check if a rectangle is empty (no zones occupy it)
     * @param x, y, width, height The rectangle to check (0-1 normalized)
     * @param excludeZoneId Zone to exclude from check (optional)
     * @return true if the rectangle is empty
     */
    bool isRectangleEmpty(qreal x, qreal y, qreal width, qreal height, const QString& excludeZoneId = QString()) const;

    /**
     * @brief Find the maximum extent a zone can expand in a given direction
     * @param zoneId The zone to expand
     * @param direction 0=left, 1=right, 2=up, 3=down
     * @return Maximum expansion amount (0-1 normalized), 0 if cannot expand
     */
    qreal findMaxExpansion(const QString& zoneId, int direction) const;

    /**
     * @brief Smart fill: find the empty region at the target position and resize to fill it
     *
     * Used when a zone overlaps with other zones - finds the empty space at
     * the mouse position (or zone center if not provided) and resizes the zone to fill that region.
     *
     * @param zoneId The zone to fill
     * @param mouseX Normalized mouse X position (0-1), or -1 to use zone center
     * @param mouseY Normalized mouse Y position (0-1), or -1 to use zone center
     * @return true if zone was repositioned and resized
     */
    bool smartFillZone(const QString& zoneId, qreal mouseX = -1, qreal mouseY = -1);

    QVariantList m_zones;

    // Default colors (can be overridden from QML with theme colors)
    QString m_defaultHighlightColor;
    QString m_defaultInactiveColor;
    QString m_defaultBorderColor;

    // Batch update support
    int m_batchUpdateDepth = 0;
    bool m_pendingZonesChanged = false;
    bool m_pendingZonesModified = false;
    QSet<QString> m_pendingColorChanges;
    QSet<QString> m_pendingGeometryChanges;
    QSet<QString> m_pendingZoneAdded;
    QSet<QString> m_pendingZoneRemoved;

    void emitDeferredSignals();
};

} // namespace PlasmaZones
