// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Layout static factory methods for predefined layout templates.
// Part of Layout class — split from layout.cpp for SRP.

#include "../layout.h"
#include "../constants.h"
#include "../zone.h"

namespace PlasmaZones {

Layout* Layout::createColumnsLayout(int columns, QObject* parent)
{
    if (columns < 1) {
        columns = 1;
    }
    auto layout = new Layout(QStringLiteral("Columns (%1)").arg(columns), parent);
    layout->setDescription(QStringLiteral("Vertical columns layout"));

    qreal columnWidth = 1.0 / columns;
    for (int i = 0; i < columns; ++i) {
        auto zone = new Zone(layout);
        zone->setRelativeGeometry(QRectF(i * columnWidth, 0, columnWidth, 1.0));
        zone->setZoneNumber(i + 1);
        zone->setName(QStringLiteral("Column %1").arg(i + 1));
        layout->m_zones.append(zone);
    }

    return layout;
}

Layout* Layout::createRowsLayout(int rows, QObject* parent)
{
    if (rows < 1) {
        rows = 1;
    }
    auto layout = new Layout(QStringLiteral("Rows (%1)").arg(rows), parent);
    layout->setDescription(QStringLiteral("Horizontal rows layout"));

    qreal rowHeight = 1.0 / rows;
    for (int i = 0; i < rows; ++i) {
        auto zone = new Zone(layout);
        zone->setRelativeGeometry(QRectF(0, i * rowHeight, 1.0, rowHeight));
        zone->setZoneNumber(i + 1);
        zone->setName(QStringLiteral("Row %1").arg(i + 1));
        layout->m_zones.append(zone);
    }

    return layout;
}

Layout* Layout::createGridLayout(int columns, int rows, QObject* parent)
{
    if (columns < 1) {
        columns = 1;
    }
    if (rows < 1) {
        rows = 1;
    }
    auto layout = new Layout(QStringLiteral("Grid (%1x%2)").arg(columns).arg(rows), parent);
    layout->setDescription(QStringLiteral("Grid layout"));

    qreal columnWidth = 1.0 / columns;
    qreal rowHeight = 1.0 / rows;
    int zoneNum = 1;

    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < columns; ++col) {
            auto zone = new Zone(layout);
            zone->setRelativeGeometry(QRectF(col * columnWidth, row * rowHeight, columnWidth, rowHeight));
            zone->setZoneNumber(zoneNum++);
            zone->setName(QStringLiteral("Cell %1,%2").arg(row + 1).arg(col + 1));
            layout->m_zones.append(zone);
        }
    }

    return layout;
}

Layout* Layout::createPriorityGridLayout(QObject* parent)
{
    auto layout = new Layout(QStringLiteral("Priority Grid"), parent);
    layout->setDescription(QStringLiteral("Large primary zone with smaller secondary zones"));

    // Main zone (left 2/3)
    auto mainZone = new Zone(layout);
    mainZone->setRelativeGeometry(QRectF(0, 0, Defaults::PriorityGridMainRatio, 1.0));
    mainZone->setZoneNumber(1);
    mainZone->setName(QStringLiteral("Primary"));
    layout->m_zones.append(mainZone);

    // Top right
    auto topRight = new Zone(layout);
    topRight->setRelativeGeometry(
        QRectF(Defaults::PriorityGridMainRatio, 0, Defaults::PriorityGridSecondaryRatio, 0.5));
    topRight->setZoneNumber(2);
    topRight->setName(QStringLiteral("Secondary Top"));
    layout->m_zones.append(topRight);

    // Bottom right
    auto bottomRight = new Zone(layout);
    bottomRight->setRelativeGeometry(
        QRectF(Defaults::PriorityGridMainRatio, 0.5, Defaults::PriorityGridSecondaryRatio, 0.5));
    bottomRight->setZoneNumber(3);
    bottomRight->setName(QStringLiteral("Secondary Bottom"));
    layout->m_zones.append(bottomRight);

    return layout;
}

Layout* Layout::createFocusLayout(QObject* parent)
{
    auto layout = new Layout(QStringLiteral("Focus"), parent);
    layout->setDescription(QStringLiteral("Large center zone with side panels"));

    // Left panel
    auto left = new Zone(layout);
    left->setRelativeGeometry(QRectF(0, 0, Defaults::FocusSideRatio, 1.0));
    left->setZoneNumber(1);
    left->setName(QStringLiteral("Left Panel"));
    layout->m_zones.append(left);

    // Center (main focus)
    auto center = new Zone(layout);
    center->setRelativeGeometry(QRectF(Defaults::FocusSideRatio, 0, Defaults::FocusMainRatio, 1.0));
    center->setZoneNumber(2);
    center->setName(QStringLiteral("Focus"));
    layout->m_zones.append(center);

    // Right panel - starts after side + main
    constexpr qreal rightStart = Defaults::FocusSideRatio + Defaults::FocusMainRatio;
    auto right = new Zone(layout);
    right->setRelativeGeometry(QRectF(rightStart, 0, Defaults::FocusSideRatio, 1.0));
    right->setZoneNumber(3);
    right->setName(QStringLiteral("Right Panel"));
    layout->m_zones.append(right);

    return layout;
}

} // namespace PlasmaZones
