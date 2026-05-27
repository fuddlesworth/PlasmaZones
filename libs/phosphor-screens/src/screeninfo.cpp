// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PhosphorScreens/ScreenInfo.h"

#include <QStringList>

namespace Phosphor::Screens {

QVariantList screenInfoListToVariantList(const QList<ScreenInfo>& screens)
{
    QVariantList list;
    list.reserve(screens.size());

    for (const ScreenInfo& s : screens) {
        QVariantMap map;
        map[QStringLiteral("name")] = s.name;
        map[QStringLiteral("isPrimary")] = s.isPrimary;
        if (!s.manufacturer.isEmpty())
            map[QStringLiteral("manufacturer")] = s.manufacturer;
        if (!s.model.isEmpty())
            map[QStringLiteral("model")] = s.model;
        if (s.width > 0 && s.height > 0) {
            map[QStringLiteral("resolution")] = QStringLiteral("%1×%2").arg(s.width).arg(s.height);
            map[QStringLiteral("width")] = s.width;
            map[QStringLiteral("height")] = s.height;
        }
        if (!s.screenId.isEmpty())
            map[QStringLiteral("screenId")] = s.screenId;
        // Always emit the flag — QML consumers that test
        // `map.isVirtualScreen` would otherwise see `undefined` for
        // physical screens (a key that's absent reads as undefined,
        // not false) and have to write `=== true` instead.
        map[QStringLiteral("isVirtualScreen")] = s.isVirtualScreen;
        if (s.isVirtualScreen) {
            map[QStringLiteral("virtualIndex")] = s.virtualIndex;
            if (!s.virtualDisplayName.isEmpty())
                map[QStringLiteral("virtualDisplayName")] = s.virtualDisplayName;
        }
        if (!s.connectorName.isEmpty())
            map[QStringLiteral("connectorName")] = s.connectorName;

        // Pre-computed display label for QML consumers (context menus,
        // selectors, etc.). Single source of truth — avoids duplicating
        // label-building logic in QML.
        QString label;
        if (s.isVirtualScreen) {
            const QString vsName =
                s.virtualDisplayName.isEmpty() ? QStringLiteral("VS%1").arg(s.virtualIndex + 1) : s.virtualDisplayName;
            QStringList parts;
            if (!s.manufacturer.isEmpty())
                parts.append(s.manufacturer);
            if (!s.model.isEmpty())
                parts.append(s.model);
            const QString monitorName = parts.isEmpty() ? s.connectorName : parts.join(QLatin1Char(' '));
            label = monitorName.isEmpty() ? vsName : vsName + QStringLiteral(" — ") + monitorName;
        } else {
            QStringList parts;
            if (!s.manufacturer.isEmpty())
                parts.append(s.manufacturer);
            if (!s.model.isEmpty())
                parts.append(s.model);
            label = parts.isEmpty() ? s.name : parts.join(QLatin1Char(' '));
        }
        if (s.width > 0 && s.height > 0) {
            label += QStringLiteral(" (%1×%2)").arg(s.width).arg(s.height);
        }
        map[QStringLiteral("displayLabel")] = label;

        list.append(map);
    }

    return list;
}

} // namespace Phosphor::Screens
