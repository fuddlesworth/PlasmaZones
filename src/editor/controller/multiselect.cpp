// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../EditorController.h"
#include "../services/ZoneManager.h"
#include "../undo/UndoController.h"
#include "../undo/commands/BatchUpdateAppearanceCommand.h"
#include "../helpers/BatchOperationScope.h"
#include "../../core/constants.h"
#include "../../core/logging.h"

#include "pz_i18n.h"
#include <QGuiApplication>
#include <QPointer>
#include <QScreen>

namespace PlasmaZones {

// ============================================================================
// Batch operations for multi-selection
// ============================================================================

void EditorController::deleteSelectedZones()
{
    if (m_selectedZoneIds.isEmpty() || !m_undoController || !m_zoneManager) {
        return;
    }

    // Copy list since we'll modify it during deletion
    QStringList zonesToDelete = m_selectedZoneIds;

    {
        BatchOperationScope scope(m_undoController, m_zoneManager,
                                  PzI18n::tr("Delete %1 Zones", "@action").arg(zonesToDelete.count()));
        for (const QString& zoneId : zonesToDelete) {
            deleteZone(zoneId);
        }
    }

    // Clear selection (already done by deleteZone removing individual zones)
    clearSelection();
}

QStringList EditorController::duplicateSelectedZones()
{
    if (m_selectedZoneIds.isEmpty() || !m_undoController || !m_zoneManager) {
        return QStringList();
    }

    // For single selection, use existing implementation
    if (m_selectedZoneIds.count() == 1) {
        QString newId = duplicateZone(m_selectedZoneIds.first());
        return newId.isEmpty() ? QStringList() : QStringList{newId};
    }

    // Copy selected zones
    QStringList zonesToDuplicate = m_selectedZoneIds;
    QStringList newZoneIds;

    {
        BatchOperationScope scope(m_undoController, m_zoneManager,
                                  PzI18n::tr("Duplicate %1 Zones", "@action").arg(zonesToDuplicate.count()));
        for (const QString& zoneId : zonesToDuplicate) {
            QString newId = duplicateZone(zoneId);
            if (!newId.isEmpty()) {
                newZoneIds.append(newId);
            }
        }
    }

    // Select all duplicated zones
    if (!newZoneIds.isEmpty()) {
        setSelectedZoneIds(newZoneIds);
    }

    return newZoneIds;
}

bool EditorController::moveSelectedZones(int direction, qreal step)
{
    if (m_selectedZoneIds.isEmpty() || !m_zoneManager) {
        return false;
    }

    // For single selection, use existing implementation
    if (m_selectedZoneIds.count() == 1) {
        return moveSelectedZone(direction, step);
    }

    // Collect all zone data first
    QList<QPair<QString, QVariantMap>> zonesToMove;
    for (const QString& zoneId : m_selectedZoneIds) {
        QVariantMap zone = m_zoneManager->getZoneById(zoneId);
        if (!zone.isEmpty()) {
            zonesToMove.append({zoneId, zone});
        }
    }

    if (zonesToMove.isEmpty()) {
        return false;
    }

    // Apply movement using RAII scope for undo macro and batch update
    {
        BatchOperationScope scope(m_undoController, m_zoneManager,
                                  PzI18n::tr("Move %1 Zones", "@action").arg(zonesToMove.count()));
        for (const auto& pair : zonesToMove) {
            const QString& zoneId = pair.first;
            const QVariantMap& zone = pair.second;
            if (ZoneManager::isFixedMode(zone)) {
                QRectF fg = m_zoneManager->extractFixedGeometry(zone);
                qreal pxStep = static_cast<qreal>(EditorConstants::KeyboardStepPixels);
                QSize ss = targetScreenSize();

                switch (direction) {
                case 0:
                    fg.moveLeft(qMax(0.0, fg.x() - pxStep));
                    break;
                case 1:
                    fg.moveLeft(qMin(static_cast<qreal>(ss.width()) - fg.width(), fg.x() + pxStep));
                    break;
                case 2:
                    fg.moveTop(qMax(0.0, fg.y() - pxStep));
                    break;
                case 3:
                    fg.moveTop(qMin(static_cast<qreal>(ss.height()) - fg.height(), fg.y() + pxStep));
                    break;
                default:
                    continue;
                }
                updateZoneGeometry(zoneId, fg.x(), fg.y(), fg.width(), fg.height(), true);
            } else {
                // Relative mode: compute delta, clamp, and apply
                qreal dx = 0.0, dy = 0.0;
                switch (direction) {
                case 0:
                    dx = -step;
                    break;
                case 1:
                    dx = step;
                    break;
                case 2:
                    dy = -step;
                    break;
                case 3:
                    dy = step;
                    break;
                default:
                    continue;
                }
                qreal x = qBound(0.0, zone[::PhosphorZones::ZoneJsonKeys::X].toDouble() + dx,
                                 1.0 - zone[::PhosphorZones::ZoneJsonKeys::Width].toDouble());
                qreal y = qBound(0.0, zone[::PhosphorZones::ZoneJsonKeys::Y].toDouble() + dy,
                                 1.0 - zone[::PhosphorZones::ZoneJsonKeys::Height].toDouble());
                updateZoneGeometry(zoneId, x, y, zone[::PhosphorZones::ZoneJsonKeys::Width].toDouble(),
                                   zone[::PhosphorZones::ZoneJsonKeys::Height].toDouble(), true);
            }
        }
    }
    return true;
}

bool EditorController::resizeSelectedZones(int direction, qreal step)
{
    if (m_selectedZoneIds.isEmpty() || !m_zoneManager) {
        return false;
    }

    // For single selection, use existing implementation
    if (m_selectedZoneIds.count() == 1) {
        return resizeSelectedZone(direction, step);
    }

    // Collect all zone data first
    QList<QPair<QString, QVariantMap>> zonesToResize;
    for (const QString& zoneId : m_selectedZoneIds) {
        QVariantMap zone = m_zoneManager->getZoneById(zoneId);
        if (!zone.isEmpty()) {
            zonesToResize.append({zoneId, zone});
        }
    }

    if (zonesToResize.isEmpty()) {
        return false;
    }

    {
        BatchOperationScope scope(m_undoController, m_zoneManager,
                                  PzI18n::tr("Resize %1 Zones", "@action").arg(zonesToResize.count()));
        for (const auto& pair : zonesToResize) {
            const QString& zoneId = pair.first;
            const QVariantMap& zone = pair.second;
            if (ZoneManager::isFixedMode(zone)) {
                QRectF fg = m_zoneManager->extractFixedGeometry(zone);
                qreal pxStep = static_cast<qreal>(EditorConstants::KeyboardStepPixels);
                qreal minFixed = static_cast<qreal>(EditorConstants::MinFixedZoneSize);
                QSize ss = targetScreenSize();

                switch (direction) {
                case 0:
                    fg.setWidth(qMax(minFixed, fg.width() - pxStep));
                    break;
                case 1:
                    fg.setWidth(qMin(static_cast<qreal>(ss.width()) - fg.x(), fg.width() + pxStep));
                    break;
                case 2:
                    fg.setHeight(qMax(minFixed, fg.height() - pxStep));
                    break;
                case 3:
                    fg.setHeight(qMin(static_cast<qreal>(ss.height()) - fg.y(), fg.height() + pxStep));
                    break;
                default:
                    continue;
                }
                fg.setWidth(qMax(minFixed, fg.width()));
                fg.setHeight(qMax(minFixed, fg.height()));
                updateZoneGeometry(zoneId, fg.x(), fg.y(), fg.width(), fg.height(), true);
            } else {
                // Relative mode
                const qreal minSize = 0.05;
                qreal x = zone[::PhosphorZones::ZoneJsonKeys::X].toDouble();
                qreal y = zone[::PhosphorZones::ZoneJsonKeys::Y].toDouble();
                qreal width = zone[::PhosphorZones::ZoneJsonKeys::Width].toDouble();
                qreal height = zone[::PhosphorZones::ZoneJsonKeys::Height].toDouble();

                switch (direction) {
                case 0:
                    width = qMax(minSize, width - step);
                    break;
                case 1:
                    width = qMin(1.0 - x, width + step);
                    break;
                case 2:
                    height = qMax(minSize, height - step);
                    break;
                case 3:
                    height = qMin(1.0 - y, height + step);
                    break;
                default:
                    continue;
                }

                if (width < minSize)
                    width = minSize;
                if (height < minSize)
                    height = minSize;

                if (x + width > 1.0) {
                    width = 1.0 - x;
                    if (width < minSize) {
                        width = minSize;
                        x = 1.0 - minSize;
                    }
                }
                if (y + height > 1.0) {
                    height = 1.0 - y;
                    if (height < minSize) {
                        height = minSize;
                        y = 1.0 - minSize;
                    }
                }

                updateZoneGeometry(zoneId, x, y, width, height, true);
            }
        }
    }
    return true;
}

// Multi-zone drag operations

void EditorController::startMultiZoneDrag(const QString& primaryZoneId, qreal startX, qreal startY)
{
    if (!m_zoneManager || primaryZoneId.isEmpty()) {
        return;
    }

    // Only activate multi-zone drag if multiple zones are selected and this zone is one of them
    if (m_selectedZoneIds.count() <= 1 || !m_selectedZoneIds.contains(primaryZoneId)) {
        m_multiZoneDragActive = false;
        return;
    }

    m_multiZoneDragActive = true;
    m_dragPrimaryZoneId = primaryZoneId;
    m_dragStartX = startX;
    m_dragStartY = startY;
    m_dragInitialPositions.clear();

    // Store initial positions of all selected zones
    for (const QString& zoneId : m_selectedZoneIds) {
        QVariantMap zone = m_zoneManager->getZoneById(zoneId);
        if (!zone.isEmpty()) {
            qreal x = zone[::PhosphorZones::ZoneJsonKeys::X].toDouble();
            qreal y = zone[::PhosphorZones::ZoneJsonKeys::Y].toDouble();
            m_dragInitialPositions[zoneId] = QPointF(x, y);
        }
    }
}

void EditorController::updateMultiZoneDrag(const QString& primaryZoneId, qreal newX, qreal newY)
{
    if (!m_multiZoneDragActive || !m_zoneManager || primaryZoneId != m_dragPrimaryZoneId) {
        return;
    }

    // Calculate delta from primary zone's starting position
    qreal dx = newX - m_dragStartX;
    qreal dy = newY - m_dragStartY;

    // Use batch update to defer signals until all zones are updated
    // This prevents QML from rebuilding mid-iteration which causes crashes
    m_zoneManager->beginBatchUpdate();

    // Update visual positions for all other selected zones
    // The primary zone is already being updated by the drag handler
    for (auto it = m_dragInitialPositions.constBegin(); it != m_dragInitialPositions.constEnd(); ++it) {
        if (it.key() == primaryZoneId) {
            continue; // Skip primary zone - it's handled by drag handler
        }

        QVariantMap zone = m_zoneManager->getZoneById(it.key());
        if (zone.isEmpty()) {
            continue;
        }

        qreal width = zone[::PhosphorZones::ZoneJsonKeys::Width].toDouble();
        qreal height = zone[::PhosphorZones::ZoneJsonKeys::Height].toDouble();

        // Calculate new position with bounds checking
        qreal newZoneX = qBound(0.0, it.value().x() + dx, 1.0 - width);
        qreal newZoneY = qBound(0.0, it.value().y() + dy, 1.0 - height);

        // Update the zone's visual position directly (without creating undo commands)
        m_zoneManager->updateZoneGeometryDirect(it.key(), newZoneX, newZoneY, width, height);
    }

    m_zoneManager->endBatchUpdate();
}

void EditorController::endMultiZoneDrag(bool commit)
{
    if (!m_multiZoneDragActive || !m_zoneManager) {
        m_multiZoneDragActive = false;
        m_dragInitialPositions.clear();
        return;
    }

    if (commit && !m_dragInitialPositions.isEmpty()) {
        // Calculate final delta from primary zone
        QVariantMap primaryZone = m_zoneManager->getZoneById(m_dragPrimaryZoneId);
        if (!primaryZone.isEmpty() && m_dragInitialPositions.contains(m_dragPrimaryZoneId)) {
            qreal finalX = primaryZone[::PhosphorZones::ZoneJsonKeys::X].toDouble();
            qreal finalY = primaryZone[::PhosphorZones::ZoneJsonKeys::Y].toDouble();
            qreal dx = finalX - m_dragInitialPositions[m_dragPrimaryZoneId].x();
            qreal dy = finalY - m_dragInitialPositions[m_dragPrimaryZoneId].y();

            // Only create undo commands for other zones (primary zone already has its own)
            if (m_undoController && (qAbs(dx) > 0.0001 || qAbs(dy) > 0.0001)) {
                m_undoController->beginMacro(
                    PzI18n::tr("Move %1 Zones", "@action").arg(m_dragInitialPositions.count()));

                for (auto it = m_dragInitialPositions.constBegin(); it != m_dragInitialPositions.constEnd(); ++it) {
                    if (it.key() == m_dragPrimaryZoneId) {
                        continue; // Skip primary - it already has undo from normal updateZoneGeometry
                    }

                    QVariantMap zone = m_zoneManager->getZoneById(it.key());
                    if (zone.isEmpty()) {
                        continue;
                    }

                    qreal width = zone[::PhosphorZones::ZoneJsonKeys::Width].toDouble();
                    qreal height = zone[::PhosphorZones::ZoneJsonKeys::Height].toDouble();
                    qreal newX = qBound(0.0, it.value().x() + dx, 1.0 - width);
                    qreal newY = qBound(0.0, it.value().y() + dy, 1.0 - height);

                    // Create undo command for this zone
                    updateZoneGeometry(it.key(), newX, newY, width, height);
                }

                m_undoController->endMacro();
            }
        }
    } else if (!commit) {
        // Cancel - restore original positions
        m_zoneManager->beginBatchUpdate();
        for (auto it = m_dragInitialPositions.constBegin(); it != m_dragInitialPositions.constEnd(); ++it) {
            if (it.key() == m_dragPrimaryZoneId) {
                continue; // Primary zone's restore is handled by its drag handler
            }

            QVariantMap zone = m_zoneManager->getZoneById(it.key());
            if (!zone.isEmpty()) {
                qreal width = zone[::PhosphorZones::ZoneJsonKeys::Width].toDouble();
                qreal height = zone[::PhosphorZones::ZoneJsonKeys::Height].toDouble();
                m_zoneManager->updateZoneGeometryDirect(it.key(), it.value().x(), it.value().y(), width, height);
            }
        }
        m_zoneManager->endBatchUpdate();
    }

    m_multiZoneDragActive = false;
    m_dragPrimaryZoneId.clear();
    m_dragInitialPositions.clear();
}

bool EditorController::isMultiZoneDragActive() const
{
    return m_multiZoneDragActive;
}

void EditorController::updateSelectedZonesAppearance(const QString& propertyName, const QVariant& value)
{
    if (m_selectedZoneIds.isEmpty() || !m_undoController || !m_zoneManager) {
        return;
    }

    // For single selection, use existing implementation
    if (m_selectedZoneIds.count() == 1) {
        updateZoneAppearance(m_selectedZoneIds.first(), propertyName, value);
        return;
    }

    // Collect old values for undo
    QMap<QString, QVariant> oldValues;
    for (const QString& zoneId : m_selectedZoneIds) {
        QVariantMap zone = m_zoneManager->getZoneById(zoneId);
        if (!zone.isEmpty()) {
            oldValues[zoneId] = zone.value(propertyName);
        }
    }

    // Use batch command for single undo step with deferred signals
    auto* command = new BatchUpdateAppearanceCommand(QPointer<ZoneManager>(m_zoneManager), m_selectedZoneIds,
                                                     propertyName, oldValues, value);
    m_undoController->push(command);
    markUnsaved();
}

void EditorController::updateSelectedZonesColor(const QString& colorType, const QString& color)
{
    if (m_selectedZoneIds.isEmpty() || !m_undoController || !m_zoneManager) {
        return;
    }

    // For single selection, use existing implementation
    if (m_selectedZoneIds.count() == 1) {
        updateZoneColor(m_selectedZoneIds.first(), colorType, color);
        return;
    }

    // Collect old colors for undo
    QMap<QString, QString> oldColors;
    for (const QString& zoneId : m_selectedZoneIds) {
        QVariantMap zone = m_zoneManager->getZoneById(zoneId);
        if (!zone.isEmpty()) {
            oldColors[zoneId] = zone.value(colorType).toString();
        }
    }

    // Use batch command for single undo step with deferred signals
    auto* command = new BatchUpdateColorCommand(QPointer<ZoneManager>(m_zoneManager), m_selectedZoneIds, colorType,
                                                oldColors, color);
    m_undoController->push(command);
    markUnsaved();
}

} // namespace PlasmaZones
