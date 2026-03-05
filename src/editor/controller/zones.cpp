// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../EditorController.h"
#include "../services/ZoneManager.h"
#include "../services/SnappingService.h"
#include "../undo/UndoController.h"
#include "../undo/commands/AddZoneCommand.h"
#include "../undo/commands/DeleteZoneCommand.h"
#include "../undo/commands/UpdateZoneGeometryCommand.h"
#include "../undo/commands/UpdateZoneNameCommand.h"
#include "../undo/commands/UpdateZoneNumberCommand.h"
#include "../undo/commands/UpdateZoneAppearanceCommand.h"
#include "../../core/constants.h"
#include "../../core/logging.h"

#include <KLocalizedString>
#include <QPointer>
#include <QtMath>

namespace PlasmaZones {

QString EditorController::addZone(qreal x, qreal y, qreal width, qreal height)
{
    if (!m_undoController || !m_zoneManager || !m_snappingService) {
        qCWarning(lcEditor) << "Services not initialized";
        return QString();
    }

    // Input validation
    if (x < 0.0 || x > 1.0 || y < 0.0 || y > 1.0 || width <= 0.0 || width > 1.0 || height <= 0.0 || height > 1.0) {
        qCWarning(lcEditor) << "Invalid zone geometry:" << x << y << width << height;
        return QString();
    }

    // Apply snapping using SnappingService
    QVariantList allZones = m_zoneManager->zones();
    QVariantMap snapped = m_snappingService->snapGeometry(x, y, width, height, allZones);
    x = snapped[JsonKeys::X].toDouble();
    y = snapped[JsonKeys::Y].toDouble();
    width = snapped[JsonKeys::Width].toDouble();
    height = snapped[JsonKeys::Height].toDouble();

    // Minimum size check
    width = qMax(EditorConstants::MinZoneSize, width);
    height = qMax(EditorConstants::MinZoneSize, height);

    // Clamp to screen bounds
    x = qBound(0.0, x, 1.0 - width);
    y = qBound(0.0, y, 1.0 - height);

    // Perform operation first to get zone ID
    QString zoneId = m_zoneManager->addZone(x, y, width, height);
    if (zoneId.isEmpty()) {
        return QString();
    }

    // Get complete zone data for undo command
    QVariantMap zoneData = m_zoneManager->getZoneById(zoneId);
    if (zoneData.isEmpty()) {
        qCWarning(lcEditor) << "Failed to get zone data after creation:" << zoneId;
        return QString();
    }

    // Create and push command (redo() will restore the zone if undone/redone)
    auto* command = new AddZoneCommand(QPointer<ZoneManager>(m_zoneManager), zoneId, zoneData, QString());
    m_undoController->push(command);

    // Select the new zone
    m_selectedZoneId = zoneId;
    Q_EMIT selectedZoneIdChanged();
    markUnsaved();

    return zoneId;
}

/**
 * @brief Updates the geometry of a zone
 * @param zoneId The unique identifier of the zone
 * @param x Relative X position (0.0-1.0)
 * @param y Relative Y position (0.0-1.0)
 * @param width Relative width (0.0-1.0)
 * @param height Relative height (0.0-1.0)
 *
 * Applies snapping and validation before updating.
 * Emits zoneGeometryChanged signal on success.
 */
void EditorController::updateZoneGeometry(const QString& zoneId, qreal x, qreal y, qreal width, qreal height,
                                          bool skipSnapping)
{
    if (!m_undoController || !m_zoneManager || !m_snappingService) {
        qCWarning(lcEditor) << "Cannot update zone geometry - services not initialized";
        return;
    }

    // Input validation
    if (zoneId.isEmpty()) {
        qCWarning(lcEditor) << "Empty zone ID for geometry update";
        return;
    }

    // Get current zone data to check geometry mode
    QVariantMap zone = m_zoneManager->getZoneById(zoneId);
    if (zone.isEmpty()) {
        qCWarning(lcEditor) << "Zone not found for geometry update:" << zoneId;
        Q_EMIT layoutSaveFailed(i18nc("@info", "Zone not found"));
        return;
    }

    int geoMode = zone.value(JsonKeys::GeometryMode, 0).toInt();
    bool isFixed = (geoMode == static_cast<int>(ZoneGeometryMode::Fixed));

    if (isFixed) {
        // Fixed mode: values are pixel coordinates — validate differently
        if (width < EditorConstants::MinFixedZoneSize || height < EditorConstants::MinFixedZoneSize) {
            qCWarning(lcEditor) << "Fixed zone too small:" << width << height;
            return;
        }
        if (x < 0.0 || y < 0.0) {
            qCWarning(lcEditor) << "Fixed zone negative position:" << x << y;
            return;
        }
    } else {
        // Relative mode: values are 0-1 normalized
        if (x < 0.0 || x > 1.0 || y < 0.0 || y > 1.0 || width <= 0.0 || width > 1.0 || height <= 0.0 || height > 1.0) {
            qCWarning(lcEditor) << "Invalid zone geometry:" << x << y << width << height;
            return;
        }
    }

    QRectF oldGeometry;
    if (isFixed) {
        oldGeometry =
            QRectF(zone.value(JsonKeys::FixedX, 0.0).toReal(), zone.value(JsonKeys::FixedY, 0.0).toReal(),
                   zone.value(JsonKeys::FixedWidth, 0.0).toReal(), zone.value(JsonKeys::FixedHeight, 0.0).toReal());
    } else {
        oldGeometry = QRectF(zone[JsonKeys::X].toReal(), zone[JsonKeys::Y].toReal(), zone[JsonKeys::Width].toReal(),
                             zone[JsonKeys::Height].toReal());
    }

    if (isFixed) {
        // Fixed mode: enforce minimum size, clamp position >= 0
        width = qMax(static_cast<qreal>(EditorConstants::MinFixedZoneSize), width);
        height = qMax(static_cast<qreal>(EditorConstants::MinFixedZoneSize), height);
        x = qMax(0.0, x);
        y = qMax(0.0, y);
    } else {
        // Relative mode: apply snapping and 0-1 clamping
        if (!skipSnapping) {
            QVariantList allZones = m_zoneManager->zones();
            QVariantMap snapped = m_snappingService->snapGeometry(x, y, width, height, allZones, zoneId);
            x = snapped[JsonKeys::X].toDouble();
            y = snapped[JsonKeys::Y].toDouble();
            width = snapped[JsonKeys::Width].toDouble();
            height = snapped[JsonKeys::Height].toDouble();
        }

        // Minimum size
        width = qMax(EditorConstants::MinZoneSize, width);
        height = qMax(EditorConstants::MinZoneSize, height);

        // Clamp to screen
        x = qBound(0.0, x, 1.0 - width);
        y = qBound(0.0, y, 1.0 - height);
    }

    QRectF newGeometry(x, y, width, height);

    // Check if geometry actually changed (within small tolerance for floating point)
    // This prevents creating undo commands when selection or sync causes no-op updates
    const qreal tolerance = 0.0001; // Very small tolerance for floating point comparison
    if (qAbs(oldGeometry.x() - newGeometry.x()) < tolerance && qAbs(oldGeometry.y() - newGeometry.y()) < tolerance
        && qAbs(oldGeometry.width() - newGeometry.width()) < tolerance
        && qAbs(oldGeometry.height() - newGeometry.height()) < tolerance) {
        // Geometry hasn't actually changed - don't create undo command
        return;
    }

    // Create and push command
    auto* command = new UpdateZoneGeometryCommand(QPointer<ZoneManager>(m_zoneManager), zoneId, oldGeometry,
                                                  newGeometry, QString());
    m_undoController->push(command);
    markUnsaved();
}

/**
 * @brief Updates the name of a zone
 * @param zoneId The unique identifier of the zone
 * @param name The new name for the zone
 */
void EditorController::updateZoneName(const QString& zoneId, const QString& name)
{
    if (!m_undoController || !m_zoneManager) {
        qCWarning(lcEditor) << "Cannot update zone name - undo controller or zone manager is null";
        Q_EMIT zoneNameValidationError(zoneId, i18nc("@info", "Services not initialized"));
        return;
    }

    // Validate zone name
    QString validationError = validateZoneName(zoneId, name);
    if (!validationError.isEmpty()) {
        Q_EMIT zoneNameValidationError(zoneId, validationError);
        return;
    }

    // Get current name for undo state
    QVariantMap zone = m_zoneManager->getZoneById(zoneId);
    if (zone.isEmpty()) {
        qCWarning(lcEditor) << "Zone not found for name update:" << zoneId;
        Q_EMIT zoneNameValidationError(zoneId, i18nc("@info", "Zone not found"));
        return;
    }

    QString oldName = zone[JsonKeys::Name].toString();

    // Create and push command
    auto* command = new UpdateZoneNameCommand(QPointer<ZoneManager>(m_zoneManager), zoneId, oldName, name, QString());
    m_undoController->push(command);
    markUnsaved();
}

/**
 * @brief Updates the number of a zone
 * @param zoneId The unique identifier of the zone
 * @param number The new zone number
 */
void EditorController::updateZoneNumber(const QString& zoneId, int number)
{
    if (!m_undoController || !m_zoneManager) {
        qCWarning(lcEditor) << "Cannot update zone number - undo controller or zone manager is null";
        Q_EMIT zoneNumberValidationError(zoneId, i18nc("@info", "Services not initialized"));
        return;
    }

    // Validate zone number
    QString validationError = validateZoneNumber(zoneId, number);
    if (!validationError.isEmpty()) {
        Q_EMIT zoneNumberValidationError(zoneId, validationError);
        return;
    }

    // Get current zone number for undo state
    QVariantMap zone = m_zoneManager->getZoneById(zoneId);
    if (zone.isEmpty()) {
        qCWarning(lcEditor) << "Zone not found for number update:" << zoneId;
        Q_EMIT zoneNumberValidationError(zoneId, i18nc("@info", "Zone not found"));
        return;
    }

    int oldNumber = zone[JsonKeys::ZoneNumber].toInt();

    // Create and push command
    auto* command =
        new UpdateZoneNumberCommand(QPointer<ZoneManager>(m_zoneManager), zoneId, oldNumber, number, QString());
    m_undoController->push(command);

    markUnsaved();
}

/**
 * @brief Updates a color property of a zone
 * @param zoneId The unique identifier of the zone
 * @param colorType The color property to update (highlightColor, inactiveColor, borderColor)
 * @param color The new color value (hex string)
 */
void EditorController::updateZoneColor(const QString& zoneId, const QString& colorType, const QString& color)
{
    if (!servicesReady("update zone color")) {
        return;
    }

    // Get current value for undo state
    QVariantMap zone = m_zoneManager->getZoneById(zoneId);
    if (zone.isEmpty()) {
        qCWarning(lcEditor) << "Zone not found for color update:" << zoneId;
        return;
    }

    QVariant oldValue = zone.value(colorType);

    // Create and push command (UpdateZoneAppearanceCommand handles color properties)
    auto* command = new UpdateZoneAppearanceCommand(QPointer<ZoneManager>(m_zoneManager), zoneId, colorType, oldValue,
                                                    color, QString());
    m_undoController->push(command);

    markUnsaved();
}

void EditorController::updateZoneAppearance(const QString& zoneId, const QString& propertyName, const QVariant& value)
{
    if (!servicesReady("update zone appearance")) {
        return;
    }

    // Get current value for undo state
    QVariantMap zone = m_zoneManager->getZoneById(zoneId);
    if (zone.isEmpty()) {
        qCWarning(lcEditor) << "Zone not found for appearance update:" << zoneId;
        return;
    }

    QVariant oldValue = zone.value(propertyName);

    // Create and push command
    auto* command = new UpdateZoneAppearanceCommand(QPointer<ZoneManager>(m_zoneManager), zoneId, propertyName,
                                                    oldValue, value, QString());
    m_undoController->push(command);
    markUnsaved();
}

/**
 * @brief Deletes a zone from the layout
 * @param zoneId The unique identifier of the zone to delete
 */
void EditorController::deleteZone(const QString& zoneId)
{
    if (!servicesReady("delete zone")) {
        return;
    }

    // Get zone data for undo
    QVariantMap zoneData = m_zoneManager->getZoneById(zoneId);
    if (zoneData.isEmpty()) {
        qCWarning(lcEditor) << "Zone not found for deletion:" << zoneId;
        Q_EMIT layoutSaveFailed(i18nc("@info", "Zone not found"));
        return;
    }

    // Create and push command
    auto* command = new DeleteZoneCommand(QPointer<ZoneManager>(m_zoneManager), zoneId, zoneData, QString());
    m_undoController->push(command);

    // Update selection state
    if (m_selectedZoneIds.contains(zoneId)) {
        m_selectedZoneIds.removeAll(zoneId);
        syncSelectionSignals();
    }

    markUnsaved();
}

int EditorController::zoneIndexById(const QString& zoneId) const
{
    if (!m_zoneManager) {
        return -1;
    }
    return m_zoneManager->findZoneIndex(zoneId);
}

QVariantMap EditorController::getZoneById(const QString& zoneId) const
{
    if (!m_zoneManager) {
        return QVariantMap();
    }
    return m_zoneManager->getZoneById(zoneId);
}

} // namespace PlasmaZones
