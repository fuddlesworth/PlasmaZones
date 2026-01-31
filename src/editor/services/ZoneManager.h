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
#include <memory>
#include <optional>

namespace PlasmaZones {

// Forward declaration for SRP helper
class ZoneAutoFiller;

/**
 * @brief Manages zone CRUD operations
 *
 * Handles zone creation, updates, deletion, duplication, and splitting.
 * Maintains the zones list and emits signals for zone changes.
 *
 * Auto-fill operations are delegated to ZoneAutoFiller (SRP).
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

    // Auto-fill operations (delegated to ZoneAutoFiller - SRP)
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

    // ═══════════════════════════════════════════════════════════════════════════════
    // DRY Helper Methods - Public for use by helper classes
    // ═══════════════════════════════════════════════════════════════════════════════

    /**
     * @brief Extract geometry from a zone map as QRectF
     * @param zone The zone QVariantMap
     * @return QRectF with x, y, width, height
     *
     * DRY: Consolidates the repeated pattern of extracting geometry fields.
     */
    QRectF extractZoneGeometry(const QVariantMap& zone) const;

    /**
     * @brief Get validated zone by ID with logging on failure
     * @param zoneId Zone ID to look up
     * @return Optional containing zone data, or empty on failure (logs warning)
     *
     * DRY: Consolidates findZoneIndex + validation + logging pattern.
     */
    std::optional<QVariantMap> getValidatedZone(const QString& zoneId) const;

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
    // Friend class for SRP helper
    friend class ZoneAutoFiller;

    /**
     * @brief Creates a new zone map with default values
     */
    QVariantMap createZone(const QString& name, int number, qreal x, qreal y, qreal width, qreal height);

    /**
     * @brief Renumbers all zones sequentially starting from 1
     */
    void renumberZones();

    // ═══════════════════════════════════════════════════════════════════════════════
    // DRY Helper Methods - Private
    // ═══════════════════════════════════════════════════════════════════════════════

    /**
     * @brief Validated geometry result
     */
    struct ValidatedGeometry {
        qreal x, y, width, height;
        bool isValid = false;
    };

    /**
     * @brief Validate and clamp zone geometry to valid bounds
     * @param x, y, width, height Input geometry (may be invalid)
     * @return ValidatedGeometry with clamped values and isValid flag
     *
     * DRY: Consolidates geometry validation repeated in addZone, updateZoneGeometry, etc.
     */
    ValidatedGeometry validateAndClampGeometry(qreal x, qreal y, qreal width, qreal height) const;

    /**
     * @brief Signal types for deferred emission
     */
    enum class SignalType {
        ZoneAdded,
        ZoneRemoved,
        GeometryChanged,
        NameChanged,
        NumberChanged,
        ColorChanged,
        ZOrderChanged
    };

    /**
     * @brief Emit zone signals with batch support
     * @param type The type of signal to emit
     * @param zoneId The zone ID
     * @param includeModified Whether to also emit zonesModified signal
     *
     * DRY: Consolidates the repeated if(batch) {...} else {...} pattern.
     */
    void emitZoneSignal(SignalType type, const QString& zoneId, bool includeModified = true);

    /**
     * @brief Update z-order values for all zones
     *
     * DRY: Consolidates the loop that updates zOrder after reordering.
     */
    void updateAllZOrderValues();

    QVariantList m_zones;

    // Default colors (can be overridden from QML with theme colors)
    QString m_defaultHighlightColor;
    QString m_defaultInactiveColor;
    QString m_defaultBorderColor;

    // Auto-fill helper (SRP - initialized after colors are set)
    std::unique_ptr<ZoneAutoFiller> m_autoFiller;

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
