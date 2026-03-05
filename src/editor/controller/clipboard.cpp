// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../EditorController.h"
#include "../services/ZoneManager.h"
#include "../undo/UndoController.h"
#include "../undo/commands/PasteZonesCommand.h"
#include "../helpers/ZoneSerialization.h"
#include "../helpers/BatchOperationScope.h"
#include "../../core/constants.h"
#include "../../core/logging.h"

#include <KLocalizedString>
#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QUuid>

namespace PlasmaZones {

void EditorController::onClipboardChanged()
{
    bool newCanPaste = canPaste();
    if (m_canPaste != newCanPaste) {
        m_canPaste = newCanPaste;
        Q_EMIT canPasteChanged();
    }
}

void EditorController::copyZones(const QStringList& zoneIds)
{
    if (!m_zoneManager) {
        qCWarning(lcEditor) << "ZoneManager not initialized";
        Q_EMIT clipboardOperationFailed(i18nc("@info", "Zone manager not initialized"));
        return;
    }

    if (zoneIds.isEmpty()) {
        qCWarning(lcEditor) << "Empty zone ID list for copy";
        return;
    }

    // Collect zones to copy
    QVariantList zonesToCopy;
    QVariantList allZones = m_zoneManager->zones();

    for (const QVariant& zoneVar : allZones) {
        QVariantMap zone = zoneVar.toMap();
        QString zoneId = zone[JsonKeys::Id].toString();
        if (zoneIds.contains(zoneId)) {
            zonesToCopy.append(zone);
        }
    }

    if (zonesToCopy.isEmpty()) {
        qCWarning(lcEditor) << "No valid zones found to copy";
        return;
    }

    // Serialize to JSON using helper
    QString jsonData = ZoneSerialization::serializeZonesToClipboard(zonesToCopy);

    // Copy to clipboard
    QClipboard* clipboard = QGuiApplication::clipboard();

    // QClipboard::setMimeData() takes ownership of QMimeData
    // No need to specify parent - ownership is transferred to clipboard
    QMimeData* mimeData = new QMimeData();
    mimeData->setData(QStringLiteral("application/vnd.plasmazones.zones+json"), jsonData.toUtf8());
    mimeData->setData(QStringLiteral("application/json"), jsonData.toUtf8());
    mimeData->setText(jsonData); // Text fallback for debugging

    // Check if clipboard state will change (we're setting valid zone data, so canPaste will be true after)
    bool wasCanPaste = canPaste();
    clipboard->setMimeData(mimeData, QClipboard::Clipboard);

    // Emit signal if clipboard state changed (we just set valid data, so canPaste is now true)
    if (!wasCanPaste) {
        m_canPaste = true;
        Q_EMIT canPasteChanged();
    }
}

void EditorController::cutZones(const QStringList& zoneIds)
{
    if (zoneIds.isEmpty() || !m_undoController) {
        return;
    }

    // Copy first
    copyZones(zoneIds);

    // Then delete with undo macro for single undo step
    {
        BatchOperationScope scope(m_undoController, m_zoneManager, i18nc("@action", "Cut %1 Zones", zoneIds.count()));
        for (const QString& zoneId : zoneIds) {
            deleteZone(zoneId);
        }
    }
}

QStringList EditorController::pasteZones(bool withOffset)
{
    if (!m_undoController || !m_zoneManager) {
        qCWarning(lcEditor) << "Cannot paste zones - undo controller or zone manager is null";
        Q_EMIT clipboardOperationFailed(i18nc("@info", "Zone manager not initialized"));
        return QStringList();
    }

    // Get clipboard data
    QClipboard* clipboard = QGuiApplication::clipboard();
    QString clipboardText = clipboard->text();

    if (clipboardText.isEmpty()) {
        return QStringList();
    }

    // Deserialize zones using helper
    QVariantList zonesToPaste = ZoneSerialization::deserializeZonesFromClipboard(clipboardText);
    if (zonesToPaste.isEmpty()) {
        return QStringList();
    }

    // Calculate offset if needed
    qreal offsetX = 0.0;
    qreal offsetY = 0.0;
    if (withOffset) {
        offsetX = EditorConstants::DuplicateOffset;
        offsetY = EditorConstants::DuplicateOffset;
    }

    // Prepare zones with new IDs and adjusted positions
    QStringList newZoneIds;
    QVariantList preparedZones;
    int newZoneNumber = m_zoneManager->zoneCount() + 1;

    for (QVariant& zoneVar : zonesToPaste) {
        QVariantMap zone = zoneVar.toMap();

        // Generate new ID
        QString newId = QUuid::createUuid().toString();
        zone[JsonKeys::Id] = newId;

        if (ZoneManager::isFixedMode(zone) && withOffset) {
            // Fixed mode: offset fixed pixel coords, skip 0-1 clamping for fixed keys
            QRectF fg = m_zoneManager->extractFixedGeometry(zone);
            fg.moveLeft(qMax(0.0, fg.x() + EditorConstants::DuplicateOffsetPixels));
            fg.moveTop(qMax(0.0, fg.y() + EditorConstants::DuplicateOffsetPixels));
            fg.setWidth(qMax(static_cast<qreal>(EditorConstants::MinFixedZoneSize), fg.width()));
            fg.setHeight(qMax(static_cast<qreal>(EditorConstants::MinFixedZoneSize), fg.height()));

            zone[JsonKeys::FixedX] = fg.x();
            zone[JsonKeys::FixedY] = fg.y();
            zone[JsonKeys::FixedWidth] = fg.width();
            zone[JsonKeys::FixedHeight] = fg.height();

            // Update relative fallback from fixed coords
            QSizeF ss = m_zoneManager->effectiveScreenSizeF();
            zone[JsonKeys::X] = fg.x() / ss.width();
            zone[JsonKeys::Y] = fg.y() / ss.height();
            zone[JsonKeys::Width] = fg.width() / ss.width();
            zone[JsonKeys::Height] = fg.height() / ss.height();
        } else {
            // Relative mode (or fixed without offset): apply relative offset and clamp
            qreal x = zone[JsonKeys::X].toDouble() + offsetX;
            qreal y = zone[JsonKeys::Y].toDouble() + offsetY;
            qreal width = zone[JsonKeys::Width].toDouble();
            qreal height = zone[JsonKeys::Height].toDouble();

            x = qBound(0.0, x, 1.0 - width);
            y = qBound(0.0, y, 1.0 - height);

            zone[JsonKeys::X] = x;
            zone[JsonKeys::Y] = y;
        }

        zone[JsonKeys::ZoneNumber] = newZoneNumber++;

        newZoneIds.append(newId);
        preparedZones.append(zone);
    }

    // Use batch update to defer signals until all zones are added
    m_zoneManager->beginBatchUpdate();

    for (const QVariant& zoneVar : preparedZones) {
        QVariantMap zone = zoneVar.toMap();
        m_zoneManager->addZoneFromMap(zone);
    }

    m_zoneManager->endBatchUpdate();

    // Create and push single command for all pasted zones (handles atomic undo/redo)
    auto* command = new PasteZonesCommand(QPointer<ZoneManager>(m_zoneManager), preparedZones,
                                          i18nc("@action", "Paste %1 Zones", preparedZones.count()));
    m_undoController->push(command);

    // Select all pasted zones
    if (!newZoneIds.isEmpty()) {
        setSelectedZoneIds(newZoneIds);
        markUnsaved();
    }

    return newZoneIds;
}

} // namespace PlasmaZones
