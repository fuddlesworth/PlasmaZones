// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../EditorController.h"
#include "../services/ZoneManager.h"
#include "../../core/constants.h"
#include "../../core/logging.h"

#include <QGuiApplication>
#include <QScreen>
#include <utility>

namespace PlasmaZones {

void EditorController::syncSelectionSignals()
{
    QString newSelectedId = m_selectedZoneIds.isEmpty() ? QString() : m_selectedZoneIds.first();
    if (m_selectedZoneId != newSelectedId) {
        m_selectedZoneId = newSelectedId;
        Q_EMIT selectedZoneIdChanged();
    }
    Q_EMIT selectedZoneIdsChanged();
}

/**
 * @brief Selects the next zone in the zone list
 * @return Zone ID of the newly selected zone, or empty string if no zones
 */
QString EditorController::selectNextZone()
{
    if (!m_zoneManager) {
        return QString();
    }

    QVariantList zones = m_zoneManager->zones();
    if (zones.isEmpty()) {
        return QString();
    }

    // Find current zone index
    int currentIndex = -1;
    if (!m_selectedZoneId.isEmpty()) {
        currentIndex = m_zoneManager->findZoneIndex(m_selectedZoneId);
    }

    // Select next zone (wrap around to first if at end)
    int nextIndex = (currentIndex + 1) % zones.length();
    QVariantMap nextZone = zones[nextIndex].toMap();
    QString nextZoneId = nextZone[::PhosphorZones::ZoneJsonKeys::Id].toString();

    setSelectedZoneId(nextZoneId);
    return nextZoneId;
}

/**
 * @brief Selects the previous zone in the zone list
 * @return Zone ID of the newly selected zone, or empty string if no zones
 */
QString EditorController::selectPreviousZone()
{
    if (!m_zoneManager) {
        return QString();
    }

    QVariantList zones = m_zoneManager->zones();
    if (zones.isEmpty()) {
        return QString();
    }

    // Find current zone index
    int currentIndex = -1;
    if (!m_selectedZoneId.isEmpty()) {
        currentIndex = m_zoneManager->findZoneIndex(m_selectedZoneId);
    }

    // Select previous zone (wrap around to last if at beginning)
    int prevIndex = currentIndex <= 0 ? zones.length() - 1 : currentIndex - 1;
    QVariantMap prevZone = zones[prevIndex].toMap();
    QString prevZoneId = prevZone[::PhosphorZones::ZoneJsonKeys::Id].toString();

    setSelectedZoneId(prevZoneId);
    return prevZoneId;
}

/**
 * @brief Moves the selected zone in the specified direction
 * @param direction 0=left, 1=right, 2=up, 3=down
 * @param step Movement step size (relative, default 0.01 = 1%)
 * @return true if zone was moved, false if no zone selected or invalid direction
 */
bool EditorController::moveSelectedZone(int direction, qreal step)
{
    if (m_selectedZoneId.isEmpty() || !m_zoneManager) {
        return false;
    }

    QVariantMap selectedZone = m_zoneManager->getZoneById(m_selectedZoneId);
    if (selectedZone.isEmpty()) {
        return false;
    }

    if (ZoneManager::isFixedMode(selectedZone)) {
        // Fixed mode: move in pixel space with screen bounds
        QRectF fg = m_zoneManager->extractFixedGeometry(selectedZone);
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
            return false;
        }

        updateZoneGeometry(m_selectedZoneId, fg.x(), fg.y(), fg.width(), fg.height(), true);
        return true;
    }

    // Relative mode: original behavior
    qreal x = selectedZone[::PhosphorZones::ZoneJsonKeys::X].toDouble();
    qreal y = selectedZone[::PhosphorZones::ZoneJsonKeys::Y].toDouble();
    qreal width = selectedZone[::PhosphorZones::ZoneJsonKeys::Width].toDouble();
    qreal height = selectedZone[::PhosphorZones::ZoneJsonKeys::Height].toDouble();

    switch (direction) {
    case 0:
        x = qMax(0.0, x - step);
        break;
    case 1:
        x = qMin(1.0 - width, x + step);
        break;
    case 2:
        y = qMax(0.0, y - step);
        break;
    case 3:
        y = qMin(1.0 - height, y + step);
        break;
    default:
        return false;
    }

    x = qBound(0.0, x, 1.0 - width);
    y = qBound(0.0, y, 1.0 - height);

    updateZoneGeometry(m_selectedZoneId, x, y, width, height, true);
    return true;
}

/**
 * @brief Resizes the selected zone in the specified direction
 * @param direction 0=left (grow left), 1=right (grow right), 2=top (grow top), 3=bottom (grow bottom)
 * @param step Resize step size (relative, default 0.01 = 1%)
 * @return true if zone was resized, false if no zone selected or invalid direction
 */
bool EditorController::resizeSelectedZone(int direction, qreal step)
{
    if (m_selectedZoneId.isEmpty() || !m_zoneManager) {
        return false;
    }

    QVariantMap selectedZone = m_zoneManager->getZoneById(m_selectedZoneId);
    if (selectedZone.isEmpty()) {
        return false;
    }

    if (ZoneManager::isFixedMode(selectedZone)) {
        // Fixed mode: resize in pixel space with screen bounds
        QRectF fg = m_zoneManager->extractFixedGeometry(selectedZone);
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
            return false;
        }
        fg.setWidth(qMax(minFixed, fg.width()));
        fg.setHeight(qMax(minFixed, fg.height()));

        updateZoneGeometry(m_selectedZoneId, fg.x(), fg.y(), fg.width(), fg.height(), true);
        return true;
    }

    // Relative mode: original behavior
    qreal x = selectedZone[::PhosphorZones::ZoneJsonKeys::X].toDouble();
    qreal y = selectedZone[::PhosphorZones::ZoneJsonKeys::Y].toDouble();
    qreal width = selectedZone[::PhosphorZones::ZoneJsonKeys::Width].toDouble();
    qreal height = selectedZone[::PhosphorZones::ZoneJsonKeys::Height].toDouble();

    const qreal minSize = 0.05;

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
        return false;
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

    updateZoneGeometry(m_selectedZoneId, x, y, width, height, true);
    return true;
}

// ============================================================================
// Multi-selection manipulation methods
// ============================================================================

void EditorController::addToSelection(const QString& zoneId)
{
    if (zoneId.isEmpty() || m_selectedZoneIds.contains(zoneId)) {
        return;
    }

    // Verify zone exists
    if (m_zoneManager && m_zoneManager->getZoneById(zoneId).isEmpty()) {
        return;
    }

    m_selectedZoneIds.append(zoneId);

    // Update single selection to first if this is the first zone
    if (m_selectedZoneIds.count() == 1) {
        m_selectedZoneId = zoneId;
        Q_EMIT selectedZoneIdChanged();
    }

    Q_EMIT selectedZoneIdsChanged();
}

void EditorController::removeFromSelection(const QString& zoneId)
{
    if (!m_selectedZoneIds.contains(zoneId)) {
        return;
    }

    m_selectedZoneIds.removeAll(zoneId);
    syncSelectionSignals();
}

void EditorController::toggleSelection(const QString& zoneId)
{
    if (m_selectedZoneIds.contains(zoneId)) {
        removeFromSelection(zoneId);
    } else {
        addToSelection(zoneId);
    }
}

void EditorController::selectRange(const QString& fromId, const QString& toId)
{
    if (!m_zoneManager || fromId.isEmpty() || toId.isEmpty()) {
        return;
    }

    QVariantList allZones = m_zoneManager->zones();
    int fromIndex = -1;
    int toIndex = -1;

    // Find indices of both zones
    for (int i = 0; i < allZones.count(); ++i) {
        QVariantMap zone = allZones[i].toMap();
        QString id = zone[::PhosphorZones::ZoneJsonKeys::Id].toString();
        if (id == fromId)
            fromIndex = i;
        if (id == toId)
            toIndex = i;
    }

    if (fromIndex < 0 || toIndex < 0) {
        return;
    }

    // Ensure from < to
    if (fromIndex > toIndex) {
        std::swap(fromIndex, toIndex);
    }

    // Select all zones in range (adds to existing selection)
    for (int i = fromIndex; i <= toIndex; ++i) {
        QVariantMap zone = allZones[i].toMap();
        QString zoneId = zone[::PhosphorZones::ZoneJsonKeys::Id].toString();
        if (!m_selectedZoneIds.contains(zoneId)) {
            m_selectedZoneIds.append(zoneId);
        }
    }

    // Update single selection for backward compatibility
    if (!m_selectedZoneIds.isEmpty() && m_selectedZoneId != m_selectedZoneIds.first()) {
        m_selectedZoneId = m_selectedZoneIds.first();
        Q_EMIT selectedZoneIdChanged();
    }

    Q_EMIT selectedZoneIdsChanged();
}

void EditorController::selectAll()
{
    if (!m_zoneManager) {
        return;
    }

    QVariantList allZones = m_zoneManager->zones();
    QStringList newSelection;

    for (const QVariant& zoneVar : allZones) {
        QVariantMap zone = zoneVar.toMap();
        newSelection.append(zone[::PhosphorZones::ZoneJsonKeys::Id].toString());
    }

    setSelectedZoneIds(newSelection);
}

void EditorController::clearSelection()
{
    if (m_selectedZoneIds.isEmpty()) {
        return;
    }

    m_selectedZoneIds.clear();
    if (!m_selectedZoneId.isEmpty()) {
        m_selectedZoneId.clear();
        Q_EMIT selectedZoneIdChanged();
    }
    Q_EMIT selectedZoneIdsChanged();
}

bool EditorController::isSelected(const QString& zoneId) const
{
    return m_selectedZoneIds.contains(zoneId);
}

bool EditorController::allSelectedUseCustomColors() const
{
    if (!m_zoneManager || m_selectedZoneIds.isEmpty()) {
        return false;
    }

    for (const QString& zoneId : m_selectedZoneIds) {
        const QVariantMap zone = m_zoneManager->getZoneById(zoneId);
        if (zone.isEmpty()) {
            return false;
        }
        // Check useCustomColors property (::PhosphorZones::ZoneJsonKeys::UseCustomColors is already QLatin1String)
        if (!zone.value(QString(::PhosphorZones::ZoneJsonKeys::UseCustomColors), false).toBool()) {
            return false;
        }
    }
    return true;
}

QStringList EditorController::selectZonesInRect(qreal x, qreal y, qreal width, qreal height, bool additive)
{
    if (!m_zoneManager || width <= 0.0 || height <= 0.0) {
        return QStringList();
    }

    const qreal rectRight = x + width;
    const qreal rectBottom = y + height;

    // Start with existing selection if additive
    QStringList selectedIds = additive ? m_selectedZoneIds : QStringList();

    // Get zones and check intersection
    const QVariantList& zonesList = m_zoneManager->zones();
    for (const QVariant& zoneVar : zonesList) {
        const QVariantMap zone = zoneVar.toMap();
        const QString zoneId = zone.value(QString(::PhosphorZones::ZoneJsonKeys::Id)).toString();
        if (zoneId.isEmpty()) {
            continue;
        }

        // Zone bounds
        const qreal zoneX = zone.value(QString(::PhosphorZones::ZoneJsonKeys::X)).toDouble();
        const qreal zoneY = zone.value(QString(::PhosphorZones::ZoneJsonKeys::Y)).toDouble();
        const qreal zoneRight = zoneX + zone.value(QString(::PhosphorZones::ZoneJsonKeys::Width)).toDouble();
        const qreal zoneBottom = zoneY + zone.value(QString(::PhosphorZones::ZoneJsonKeys::Height)).toDouble();

        // Check AABB intersection
        const bool intersects = !(zoneRight < x || zoneX > rectRight || zoneBottom < y || zoneY > rectBottom);

        if (intersects && !selectedIds.contains(zoneId)) {
            selectedIds.append(zoneId);
        }
    }

    // Update selection if we found any zones
    if (!selectedIds.isEmpty()) {
        setSelectedZoneIds(selectedIds);
    }

    return selectedIds;
}

} // namespace PlasmaZones
