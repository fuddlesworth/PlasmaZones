// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../EditorController.h"
#include "../services/ZoneManager.h"
#include "../undo/UndoController.h"
#include "../undo/commands/BatchUpdateAppearanceCommand.h"
#include "../helpers/BatchOperationScope.h"
#include "core/types/constants.h"
#include "core/platform/logging.h"

#include "phosphor_i18n.h"
#include <QGuiApplication>
#include <QPair>
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
                                  PhosphorI18n::tr("Delete %1 Zones", "@action").arg(zonesToDelete.count()));
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
                                  PhosphorI18n::tr("Duplicate %1 Zones", "@action").arg(zonesToDuplicate.count()));
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
                                  PhosphorI18n::tr("Move %1 Zones", "@action").arg(zonesToMove.count()));
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
                                  PhosphorI18n::tr("Resize %1 Zones", "@action").arg(zonesToResize.count()));
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
//
// The drag delta arrives in relative units (the primary zone's canvas
// position over the canvas size). Every selected zone moves by it in its own
// unit: a relative zone directly, a fixed zone by the delta scaled to the
// reference in pixels. Both ZoneManager::updateZoneGeometryDirect and
// updateZoneGeometry take pixels for a fixed zone, and a relative position
// handed to them reads as a sub-minimum pixel rect, collapsing the zone.

namespace {

/// Where a selected zone lands when the primary moved by (@p dx, @p dy)
/// relative units from the drag's start, as the (x, y, width, height) its
/// own unit takes. @p initial is its start position in that unit.
QRectF multiDragTarget(const ZoneManager& zoneManager, const QVariantMap& zone, const QPointF& initial, qreal dx,
                       qreal dy)
{
    if (ZoneManager::isFixedMode(zone)) {
        const QRectF fixed = zoneManager.extractFixedGeometry(zone);
        const QSizeF reference = zoneManager.effectiveScreenSizeF();
        // Spelled out rather than qBound: a fixed zone wider than the
        // reference has no room to move, and qBound requires min <= max.
        const qreal x = qMax(0.0, qMin(reference.width() - fixed.width(), initial.x() + dx * reference.width()));
        const qreal y = qMax(0.0, qMin(reference.height() - fixed.height(), initial.y() + dy * reference.height()));
        return QRectF(x, y, fixed.width(), fixed.height());
    }
    const qreal width = zone[::PhosphorZones::ZoneJsonKeys::Width].toDouble();
    const qreal height = zone[::PhosphorZones::ZoneJsonKeys::Height].toDouble();
    return QRectF(qBound(0.0, initial.x() + dx, 1.0 - width), qBound(0.0, initial.y() + dy, 1.0 - height), width,
                  height);
}

/// The zone's position in its own unit, which is what the drag records.
QPointF multiDragOrigin(const ZoneManager& zoneManager, const QVariantMap& zone)
{
    if (ZoneManager::isFixedMode(zone)) {
        return zoneManager.extractFixedGeometry(zone).topLeft();
    }
    return zoneManager.extractZoneGeometry(zone).topLeft();
}

/// Put a non-primary zone back at its recorded drag start, @p start in the
/// zone's own unit, as no edit. Goes through a geometry snapshot rather than
/// updateZoneGeometryDirect, which clamps a fixed zone's origin to zero and
/// would move one that started at a negative offset.
void restoreMultiDragStart(ZoneManager& zoneManager, const QString& zoneId, const QPointF& start)
{
    ZoneGeometrySnapshot snapshot = zoneManager.geometrySnapshot(zoneId);
    if (snapshot.zoneId.isEmpty()) {
        return;
    }
    if (snapshot.fixed.isValid()) {
        snapshot.fixed.moveTopLeft(start);
    } else {
        snapshot.relative.moveTopLeft(start);
    }
    zoneManager.restoreGeometrySnapshot(snapshot, /*markModified*/ false);
}

} // anonymous namespace

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

    // Store initial positions of all selected zones, each in its own unit.
    // The primary's is read in relative units below, where the delta it
    // defines is computed, so it is recorded relative whatever its mode.
    for (const QString& zoneId : m_selectedZoneIds) {
        QVariantMap zone = m_zoneManager->getZoneById(zoneId);
        if (!zone.isEmpty()) {
            m_dragInitialPositions[zoneId] = zoneId == primaryZoneId
                ? m_zoneManager->extractZoneGeometry(zone).topLeft()
                : multiDragOrigin(*m_zoneManager, zone);
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

        // Update the zone's visual position directly (without creating undo commands)
        const QRectF target = multiDragTarget(*m_zoneManager, zone, it.value(), dx, dy);
        m_zoneManager->updateZoneGeometryDirect(it.key(), target.x(), target.y(), target.width(), target.height());
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

    // Every zone goes back unless a commit records the move. Cancel restores,
    // and so does a commit that moved nothing or whose primary zone is gone
    // (no delta to commit by). Neither creates a command, but the preview may
    // have shifted a zone anyway: multiDragTarget clamps a fixed zone into the
    // reference, so one parked at a negative offset or past an edge moved on
    // the first preview tick and would otherwise stay moved with no undo and
    // no unsaved mark.
    bool restoreStarts = true;
    if (commit && !m_dragInitialPositions.isEmpty()) {
        // Calculate final delta from primary zone
        QVariantMap primaryZone = m_zoneManager->getZoneById(m_dragPrimaryZoneId);
        if (!primaryZone.isEmpty() && m_dragInitialPositions.contains(m_dragPrimaryZoneId)) {
            qreal finalX = primaryZone[::PhosphorZones::ZoneJsonKeys::X].toDouble();
            qreal finalY = primaryZone[::PhosphorZones::ZoneJsonKeys::Y].toDouble();
            qreal dx = finalX - m_dragInitialPositions[m_dragPrimaryZoneId].x();
            qreal dy = finalY - m_dragInitialPositions[m_dragPrimaryZoneId].y();
            const bool moved = qAbs(dx) > 0.0001 || qAbs(dy) > 0.0001;
            restoreStarts = !moved;

            // Only create undo commands for other zones (primary zone already has its own)
            if (m_undoController && moved) {
                m_undoController->beginMacro(
                    PhosphorI18n::tr("Move %1 Zones", "@action").arg(m_dragInitialPositions.count()));

                for (auto it = m_dragInitialPositions.constBegin(); it != m_dragInitialPositions.constEnd(); ++it) {
                    if (it.key() == m_dragPrimaryZoneId) {
                        continue; // Skip primary - it already has undo from normal updateZoneGeometry
                    }

                    const QVariantMap zone = m_zoneManager->getZoneById(it.key());
                    if (zone.isEmpty()) {
                        continue;
                    }
                    const QRectF target = multiDragTarget(*m_zoneManager, zone, it.value(), dx, dy);

                    // The preview already moved the zone to (about) the target,
                    // so a command built from where it sits now would record no
                    // change and undo could not bring it back. Put it at its
                    // start without a command, then move it by one, so the
                    // command's old geometry is where the drag began.
                    restoreMultiDragStart(*m_zoneManager, it.key(), it.value());
                    // Skip snapping: the zone moves rigidly with the primary,
                    // whose own commit already snapped.
                    updateZoneGeometry(it.key(), target.x(), target.y(), target.width(), target.height(),
                                       /*skipSnapping*/ true);
                }

                m_undoController->endMacro();
            }
        }
    }

    if (restoreStarts) {
        m_zoneManager->beginBatchUpdate();
        for (auto it = m_dragInitialPositions.constBegin(); it != m_dragInitialPositions.constEnd(); ++it) {
            if (it.key() == m_dragPrimaryZoneId) {
                continue; // Primary zone's restore is handled by its drag handler
            }
            // The recorded start is in the zone's own unit, so it goes back
            // exactly, unclamped (a fixed zone may have started past the edge).
            restoreMultiDragStart(*m_zoneManager, it.key(), it.value());
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

    // Switching custom colours on seeds the colours of the zones it switches,
    // in the same undo step. Zones that already had custom colours keep them.
    QStringList enabling;
    if (propertyName.compare(::PhosphorZones::ZoneJsonKeys::UseCustomColors, Qt::CaseInsensitive) == 0
        && value.toBool()) {
        for (auto it = oldValues.constBegin(); it != oldValues.constEnd(); ++it) {
            if (!it.value().toBool()) {
                enabling.append(it.key());
            }
        }
    }
    const QString text = PhosphorI18n::tr("Update Appearance for %1 Zones", "@action").arg(m_selectedZoneIds.count());
    if (!enabling.isEmpty()) {
        m_undoController->beginMacro(text);
    }

    // Use batch command for single undo step with deferred signals
    auto* command = new BatchUpdateAppearanceCommand(QPointer<ZoneManager>(m_zoneManager), m_selectedZoneIds,
                                                     propertyName, oldValues, value, text);
    m_undoController->push(command);

    if (!enabling.isEmpty()) {
        seedCustomColors(enabling);
        m_undoController->endMacro();
    }
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
