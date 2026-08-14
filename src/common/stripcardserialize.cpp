// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "stripcardserialize.h"

#include <QVariantMap>

namespace PlasmaZones {

QVariantList stripColumnsToVariantList(const PhosphorScrollEngine::ScrollStripSnapshot& snapshot)
{
    QVariantList columns;
    columns.reserve(snapshot.columns.size());
    for (int ci = 0; ci < snapshot.columns.size(); ++ci) {
        const PhosphorScrollEngine::ScrollStripSnapshotColumn& column = snapshot.columns.at(ci);
        QVariantList tiles;
        tiles.reserve(column.tiles.size());
        for (const PhosphorScrollEngine::ScrollStripSnapshotTile& tile : column.tiles) {
            QVariantMap tileMap;
            tileMap.insert(QStringLiteral("windowId"), tile.windowId);
            tileMap.insert(QStringLiteral("x"), tile.relRect.x());
            tileMap.insert(QStringLiteral("y"), tile.relRect.y());
            tileMap.insert(QStringLiteral("width"), tile.relRect.width());
            tileMap.insert(QStringLiteral("height"), tile.relRect.height());
            tileMap.insert(QStringLiteral("minimized"), tile.minimized);
            tileMap.insert(QStringLiteral("hidden"), tile.hidden);
            tileMap.insert(QStringLiteral("activeTab"), tile.activeTab);
            tiles.append(tileMap);
        }
        QVariantMap columnMap;
        columnMap.insert(QStringLiteral("tabbed"), column.tabbed);
        columnMap.insert(QStringLiteral("active"), ci == snapshot.activeColumnIndex);
        columnMap.insert(QStringLiteral("relWidth"), column.relWidth);
        columnMap.insert(QStringLiteral("relHeight"), column.relHeight);
        columnMap.insert(QStringLiteral("tiles"), tiles);
        columns.append(columnMap);
    }
    return columns;
}

} // namespace PlasmaZones
