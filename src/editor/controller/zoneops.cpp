// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../EditorController.h"
#include "../services/ZoneManager.h"
#include "../services/TemplateService.h"
#include "../undo/UndoController.h"
#include "../undo/commands/FillZoneCommand.h"
#include "../undo/commands/DeleteZoneWithFillCommand.h"
#include "../undo/commands/ChangeZOrderCommand.h"
#include "../undo/commands/DuplicateZoneCommand.h"
#include "../undo/commands/ApplyTemplateCommand.h"
#include "../undo/commands/ClearAllZonesCommand.h"
#include "../undo/commands/SplitZoneCommand.h"
#include "../undo/commands/DividerResizeCommand.h"
#include "../../core/constants.h"
#include "../../core/logging.h"

#include "pz_i18n.h"
#include <QPointer>
#include <utility>

namespace PlasmaZones {

/**
 * @brief Find zones adjacent to the given zone
 * @param zoneId The zone to find neighbors for
 * @return Map with "left", "right", "top", "bottom" lists of adjacent zone IDs
 */
QVariantMap EditorController::findAdjacentZones(const QString& zoneId)
{
    if (!m_zoneManager) {
        qCWarning(lcEditor) << "ZoneManager not initialized";
        return QVariantMap();
    }

    return m_zoneManager->findAdjacentZones(zoneId);
}

/**
 * @brief Expand a zone to fill available empty space around it
 * @param zoneId The zone to expand
 * @param mouseX Normalized mouse X position (0-1), or -1 to use zone center
 * @param mouseY Normalized mouse Y position (0-1), or -1 to use zone center
 * @return true if any expansion occurred
 */
bool EditorController::expandToFillSpace(const QString& zoneId, qreal mouseX, qreal mouseY)
{
    if (!servicesReady("expand zone")) {
        return false;
    }

    // Get old geometry for undo state
    QVariantMap zone = m_zoneManager->getZoneById(zoneId);
    if (zone.isEmpty()) {
        qCWarning(lcEditor) << "Zone not found for fill:" << zoneId;
        return false;
    }

    QRectF oldGeometry(zone[JsonKeys::X].toReal(), zone[JsonKeys::Y].toReal(), zone[JsonKeys::Width].toReal(),
                       zone[JsonKeys::Height].toReal());

    // Perform operation
    bool result = m_zoneManager->expandToFillSpace(zoneId, mouseX, mouseY);
    if (!result) {
        return false;
    }

    // Get new geometry after operation
    QVariantMap updatedZone = m_zoneManager->getZoneById(zoneId);
    if (updatedZone.isEmpty()) {
        return false;
    }

    QRectF newGeometry(updatedZone[JsonKeys::X].toReal(), updatedZone[JsonKeys::Y].toReal(),
                       updatedZone[JsonKeys::Width].toReal(), updatedZone[JsonKeys::Height].toReal());

    // Create and push command
    auto* command =
        new FillZoneCommand(QPointer<ZoneManager>(m_zoneManager), zoneId, oldGeometry, newGeometry, QString());
    m_undoController->push(command);

    markUnsaved();
    return true;
}

QVariantMap EditorController::calculateFillRegion(const QString& zoneId, qreal mouseX, qreal mouseY)
{
    if (!m_zoneManager) {
        return QVariantMap();
    }
    return m_zoneManager->calculateFillRegion(zoneId, mouseX, mouseY);
}

/**
 * @brief Delete a zone and optionally expand neighbors to fill the gap
 * @param zoneId The zone to delete
 * @param autoFill If true, expand adjacent zones to fill the deleted zone's space
 */
void EditorController::deleteZoneWithFill(const QString& zoneId, bool autoFill)
{
    if (!servicesReady("delete zone with fill")) {
        return;
    }

    // Get old zones list before operation
    QVariantList oldZones = m_zoneManager->zones();

    // Get deleted zone data
    QVariantMap deletedZoneData = m_zoneManager->getZoneById(zoneId);
    if (deletedZoneData.isEmpty()) {
        qCWarning(lcEditor) << "Zone not found for deletion with fill:" << zoneId;
        return;
    }

    // Perform operation
    m_zoneManager->deleteZoneWithFill(zoneId, autoFill);

    // Get new zones list after operation
    QVariantList newZones = m_zoneManager->zones();

    // Create and push command
    auto* command = new DeleteZoneWithFillCommand(QPointer<ZoneManager>(m_zoneManager), zoneId, deletedZoneData,
                                                  oldZones, newZones, QString());
    m_undoController->push(command);

    // Update selection state
    if (m_selectedZoneIds.contains(zoneId)) {
        m_selectedZoneIds.removeAll(zoneId);
        syncSelectionSignals();
    }

    markUnsaved();
}

// ═══════════════════════════════════════════════════════════════════════════
// Z-ORDER OPERATIONS
// ═══════════════════════════════════════════════════════════════════════════

void EditorController::changeZOrderImpl(const QString& zoneId, ZOrderOp op, const QString& actionName)
{
    if (!servicesReady("change z-order")) {
        return;
    }

    QVariantList oldZones = m_zoneManager->zones();

    switch (op) {
    case ZOrderOp::BringToFront:
        m_zoneManager->bringToFront(zoneId);
        break;
    case ZOrderOp::SendToBack:
        m_zoneManager->sendToBack(zoneId);
        break;
    case ZOrderOp::BringForward:
        m_zoneManager->bringForward(zoneId);
        break;
    case ZOrderOp::SendBackward:
        m_zoneManager->sendBackward(zoneId);
        break;
    }

    QVariantList newZones = m_zoneManager->zones();
    auto* command =
        new ChangeZOrderCommand(QPointer<ZoneManager>(m_zoneManager), zoneId, oldZones, newZones, actionName);
    m_undoController->push(command);
    markUnsaved();
}

void EditorController::bringToFront(const QString& zoneId)
{
    changeZOrderImpl(zoneId, ZOrderOp::BringToFront, PzI18n::tr("Bring to Front", "@action"));
}

void EditorController::sendToBack(const QString& zoneId)
{
    changeZOrderImpl(zoneId, ZOrderOp::SendToBack, PzI18n::tr("Send to Back", "@action"));
}

void EditorController::bringForward(const QString& zoneId)
{
    changeZOrderImpl(zoneId, ZOrderOp::BringForward, PzI18n::tr("Bring Forward", "@action"));
}

void EditorController::sendBackward(const QString& zoneId)
{
    changeZOrderImpl(zoneId, ZOrderOp::SendBackward, PzI18n::tr("Send Backward", "@action"));
}

/**
 * @brief Creates a duplicate of an existing zone
 * @param zoneId The unique identifier of the zone to duplicate
 * @return Zone ID of the new zone, or empty string on failure
 */
QString EditorController::duplicateZone(const QString& zoneId)
{
    if (!servicesReady("duplicate zone")) {
        return QString();
    }

    // Get source zone data BEFORE operation (for command state)
    QVariantMap sourceZoneData = m_zoneManager->getZoneById(zoneId);
    if (sourceZoneData.isEmpty()) {
        qCWarning(lcEditor) << "Source zone not found for duplication:" << zoneId;
        return QString();
    }

    // Calculate duplicate zone data (offset position, new ID will be generated in redo())
    qreal x = sourceZoneData[JsonKeys::X].toDouble();
    qreal y = sourceZoneData[JsonKeys::Y].toDouble();
    qreal width = sourceZoneData[JsonKeys::Width].toDouble();
    qreal height = sourceZoneData[JsonKeys::Height].toDouble();
    QString sourceName = sourceZoneData[JsonKeys::Name].toString();

    // Calculate offset position
    qreal newX = x + EditorConstants::DuplicateOffset;
    qreal newY = y + EditorConstants::DuplicateOffset;
    newX = qMin(newX, 1.0 - width);
    newY = qMin(newY, 1.0 - height);

    // Create duplicate zone data (new ID will be generated in redo())
    QVariantMap duplicatedZoneData = sourceZoneData;
    duplicatedZoneData[JsonKeys::Id] = QString(); // Empty ID - will be generated in redo()
    duplicatedZoneData[JsonKeys::X] = newX;
    duplicatedZoneData[JsonKeys::Y] = newY;
    duplicatedZoneData[JsonKeys::Name] = QString(sourceName + QStringLiteral(" (Copy)"));

    // Create command (redo() will perform the operation)
    // We need to get the new zone ID after redo() is called, so we'll use a temporary approach
    // Actually, we need the zone ID to select it, so we'll perform the operation and then push
    // But we need to make redo() idempotent

    // Perform operation to get zone ID for selection
    QString newZoneId = m_zoneManager->duplicateZone(zoneId);
    if (newZoneId.isEmpty()) {
        return QString();
    }

    // Get the actual duplicated zone data (with the generated ID)
    QVariantMap actualDuplicatedZoneData = m_zoneManager->getZoneById(newZoneId);
    if (actualDuplicatedZoneData.isEmpty()) {
        qCWarning(lcEditor) << "Failed to get duplicated zone data";
        return QString();
    }

    // Update the zone data with the actual ID
    duplicatedZoneData[JsonKeys::Id] = newZoneId;

    // Create and push command (redo() will be called automatically, but zone already exists)
    auto* command = new DuplicateZoneCommand(QPointer<ZoneManager>(m_zoneManager), zoneId, newZoneId,
                                             duplicatedZoneData, QString());
    m_undoController->push(command);

    if (!newZoneId.isEmpty()) {
        m_selectedZoneId = newZoneId;
        Q_EMIT selectedZoneIdChanged();
        markUnsaved();
    }

    return newZoneId;
}

/**
 * @brief Applies a template layout to the editor
 * @param templateType The type of template (grid, columns, rows, priority, focus)
 * @param columns Number of columns (for grid/columns templates)
 * @param rows Number of rows (for grid/rows templates)
 *
 * Clears existing zones and creates new zones based on the template.
 * Validates input parameters and uses default values if invalid.
 */
void EditorController::applyTemplate(const QString& templateType, int columns, int rows)
{
    if (!m_undoController || !m_templateService || !m_zoneManager) {
        qCWarning(lcEditor) << "Services not initialized";
        return;
    }

    // Get old zones for undo
    QVariantList oldZones = m_zoneManager->zones();

    QVariantList zones = m_templateService->applyTemplate(templateType, columns, rows);
    if (zones.isEmpty()) {
        qCWarning(lcEditor) << "Template application failed for" << templateType;
        return;
    }

    // Update template zones to use theme-based default colors if they're using hardcoded defaults
    QString defaultHighlight = m_defaultHighlightColor.isEmpty()
        ? QString::fromLatin1(EditorConstants::DefaultHighlightColor)
        : m_defaultHighlightColor;
    QString defaultInactive = m_defaultInactiveColor.isEmpty()
        ? QString::fromLatin1(EditorConstants::DefaultInactiveColor)
        : m_defaultInactiveColor;
    QString defaultBorder = m_defaultBorderColor.isEmpty() ? QString::fromLatin1(EditorConstants::DefaultBorderColor)
                                                           : m_defaultBorderColor;

    for (QVariant& zoneVar : zones) {
        QVariantMap zone = zoneVar.toMap();
        // Only update if using the old hardcoded defaults
        QString currentHighlight = zone[JsonKeys::HighlightColor].toString();
        QString currentInactive = zone[JsonKeys::InactiveColor].toString();
        QString currentBorder = zone[JsonKeys::BorderColor].toString();

        if (currentHighlight == QLatin1String(EditorConstants::DefaultHighlightColor)) {
            zone[JsonKeys::HighlightColor] = defaultHighlight;
        }
        if (currentInactive == QLatin1String(EditorConstants::DefaultInactiveColor)) {
            zone[JsonKeys::InactiveColor] = defaultInactive;
        }
        if (currentBorder == QLatin1String(EditorConstants::DefaultBorderColor)) {
            zone[JsonKeys::BorderColor] = defaultBorder;
        }
        zoneVar = zone;
    }

    // Create and push command
    auto* command =
        new ApplyTemplateCommand(QPointer<ZoneManager>(m_zoneManager), templateType, oldZones, zones, QString());
    m_undoController->push(command);

    m_selectedZoneId.clear();
    m_selectedZoneIds.clear();
    Q_EMIT selectedZoneIdChanged();
    Q_EMIT selectedZoneIdsChanged();
    markUnsaved();
}

/**
 * @brief Removes all zones from the layout
 *
 * Clears the zones list and deselects any selected zone.
 */
void EditorController::clearAllZones()
{
    if (!servicesReady("clear zones")) {
        return;
    }

    // Get old zones for undo
    QVariantList oldZones = m_zoneManager->zones();

    // Create and push command
    auto* command = new ClearAllZonesCommand(QPointer<ZoneManager>(m_zoneManager), oldZones, QString());
    m_undoController->push(command);

    m_selectedZoneId.clear();
    m_selectedZoneIds.clear();
    Q_EMIT selectedZoneIdChanged();
    Q_EMIT selectedZoneIdsChanged();
    markUnsaved();
}

/**
 * @brief Finds zones that share an edge with the specified zone
 * @param zoneId The unique identifier of the zone
 * @param edgeX X coordinate of the edge to check (relative 0.0-1.0)
 * @param edgeY Y coordinate of the edge to check (relative 0.0-1.0)
 * @param threshold Distance threshold for edge detection (default 0.01)
 * @return QVariantList of zone information maps
 *
 * Used by divider system to find zones adjacent to a given edge.
 */
QVariantList EditorController::getZonesSharingEdge(const QString& zoneId, qreal edgeX, qreal edgeY, qreal threshold)
{
    if (!m_zoneManager) {
        qCWarning(lcEditor) << "ZoneManager not initialized";
        return QVariantList();
    }

    return m_zoneManager->getZonesSharingEdge(zoneId, edgeX, edgeY, threshold);
}

/**
 * @brief Splits a zone horizontally or vertically into two zones
 * @param zoneId The unique identifier of the zone to split
 * @param horizontal If true, split horizontally (top/bottom), otherwise vertically (left/right)
 * @return Zone ID of the newly created zone, or empty string on failure
 */
QString EditorController::splitZone(const QString& zoneId, bool horizontal)
{
    if (!servicesReady("split zone")) {
        return QString();
    }

    // Get original zone data before split
    QVariantMap originalZoneData = m_zoneManager->getZoneById(zoneId);
    if (originalZoneData.isEmpty()) {
        qCWarning(lcEditor) << "Zone not found for split:" << zoneId;
        return QString();
    }

    // Perform operation
    QString newZoneId = m_zoneManager->splitZone(zoneId, horizontal);
    if (newZoneId.isEmpty()) {
        return QString();
    }

    // Get new zones data (modified original + new zone)
    QVariantMap modifiedOriginalZone = m_zoneManager->getZoneById(zoneId);
    QVariantMap newZone = m_zoneManager->getZoneById(newZoneId);
    QVariantList newZonesData;
    newZonesData.append(modifiedOriginalZone);
    newZonesData.append(newZone);

    // Create and push command
    auto* command =
        new SplitZoneCommand(QPointer<ZoneManager>(m_zoneManager), zoneId, originalZoneData, newZonesData, QString());
    m_undoController->push(command);

    markUnsaved();
    return newZoneId;
}

/**
 * @brief Resizes zones at a divider position
 * @param zoneId1 Zone ID on one side of the divider
 * @param zoneId2 Zone ID on the other side of the divider
 * @param newDividerX New X position of divider (relative 0.0-1.0) for vertical dividers
 * @param newDividerY New Y position of divider (relative 0.0-1.0) for horizontal dividers
 * @param isVertical true for vertical divider, false for horizontal
 *
 * Resizes all zones on both sides of a divider to the new position.
 * Ensures zones maintain minimum size and don't overlap.
 * Emits zoneGeometryChanged for each affected zone.
 */
void EditorController::resizeZonesAtDivider(const QString& zoneId1, const QString& zoneId2, qreal newDividerX,
                                            qreal newDividerY, bool isVertical)
{
    if (!servicesReady("resize zones at divider")) {
        return;
    }

    auto oldGeometries = m_zoneManager->collectGeometriesAtDivider(zoneId1, zoneId2, isVertical);
    if (oldGeometries.isEmpty()) {
        qCWarning(lcEditor) << "No zones affected by divider resize";
        return;
    }

    auto* command = new DividerResizeCommand(QPointer<ZoneManager>(m_zoneManager), zoneId1, zoneId2, newDividerX,
                                             newDividerY, isVertical, std::move(oldGeometries), QString());
    m_undoController->push(command);
    markUnsaved();
}

} // namespace PlasmaZones
