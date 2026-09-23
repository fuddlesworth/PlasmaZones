// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "BaseZoneCommand.h"
#include <QRectF>
#include <QString>
#include <QVector>
#include <QPair>

namespace PlasmaZones {

/**
 * @brief Command for resizing zones at a divider (drag between zones)
 *
 * Undo restores all affected zones to their previous geometry.
 */
class DividerResizeCommand : public BaseZoneCommand
{
public:
    using GeometryMap = QVector<QPair<QString, QRectF>>;

    explicit DividerResizeCommand(QPointer<ZoneManager> zoneManager, const QString& zoneId1, const QString& zoneId2,
                                  qreal newDividerX, qreal newDividerY, bool isVertical, GeometryMap&& oldGeometries,
                                  const QString& text, QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;
    int id() const override
    {
        return -1;
    } // No merging

private:
    QString m_zoneId1;
    QString m_zoneId2;
    qreal m_newDividerX;
    qreal m_newDividerY;
    bool m_isVertical;
    GeometryMap m_oldGeometries;
};

} // namespace PlasmaZones
