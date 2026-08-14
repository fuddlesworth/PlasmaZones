// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "stripcardserialize.h"

#include <QVariantMap>

namespace PlasmaZones {

namespace {

// ─── Keys ─────────────────────────────────────────────────────────────────
// QLatin1String instances referenced from every insert so there is exactly
// one place each wire spelling lives — same shape as layoutpreviewserialize.
// These keys are read BY NAME in ZoneSelectorStripCard.qml, so a typo here
// is a silent runtime miss, not a compile error.
namespace K {
constexpr QLatin1String X{"x"};
constexpr QLatin1String Y{"y"};
constexpr QLatin1String Width{"width"};
constexpr QLatin1String Height{"height"};
constexpr QLatin1String ActiveTab{"activeTab"};
constexpr QLatin1String Tabbed{"tabbed"};
constexpr QLatin1String Active{"active"};
constexpr QLatin1String WidthFraction{"widthFraction"};
constexpr QLatin1String Tiles{"tiles"};
} // namespace K

} // namespace

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
            tileMap.insert(K::X, tile.relRect.x());
            tileMap.insert(K::Y, tile.relRect.y());
            tileMap.insert(K::Width, tile.relRect.width());
            tileMap.insert(K::Height, tile.relRect.height());
            tileMap.insert(K::ActiveTab, tile.activeTab);
            tiles.append(tileMap);
        }
        QVariantMap columnMap;
        columnMap.insert(K::Tabbed, column.tabbed);
        columnMap.insert(K::Active, ci == snapshot.activeColumnIndex);
        columnMap.insert(K::WidthFraction, column.widthFraction);
        columnMap.insert(K::Tiles, tiles);
        columns.append(columnMap);
    }
    return columns;
}

} // namespace PlasmaZones
