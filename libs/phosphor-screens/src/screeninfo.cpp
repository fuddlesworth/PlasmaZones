// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PhosphorScreens/ScreenInfo.h"

#include <QDebug>
#include <QStringList>

namespace Phosphor::Screens {

QVariantList screenInfoListToVariantList(const QList<ScreenInfo>& screens)
{
    QVariantList list;
    list.reserve(screens.size());

    const auto vendorModelParts = [](const ScreenInfo& info) -> QStringList {
        QStringList parts;
        if (!info.manufacturer.isEmpty())
            parts.append(info.manufacturer);
        if (!info.model.isEmpty())
            parts.append(info.model);
        return parts;
    };

    for (const ScreenInfo& s : screens) {
        // Sanity-check the virtual identity payload. The producer
        // contract is virtualIndex ≥ 0 when isVirtualScreen is true
        // (header docstring). A negative index plus the truthy flag
        // surfaces as "VS0" via `virtualIndex + 1`, which is
        // misleading; warn so producers find the regression instead
        // of silently shipping a wrong tile label.
        if (s.isVirtualScreen && s.virtualIndex < 0) {
            qWarning(
                "Phosphor::Screens::screenInfoListToVariantList: virtual screen with virtualIndex < 0 (= %d) — "
                "label will report VS%d",
                s.virtualIndex, s.virtualIndex + 1);
        }

        QVariantMap map;
        map[QStringLiteral("name")] = s.name;
        map[QStringLiteral("isPrimary")] = s.isPrimary;
        if (!s.manufacturer.isEmpty())
            map[QStringLiteral("manufacturer")] = s.manufacturer;
        if (!s.model.isEmpty())
            map[QStringLiteral("model")] = s.model;
        // Emit each dimension independently — the previous all-or-
        // nothing form dropped both width and height when only one
        // was positive, even though consumers binding `map.width`
        // separately can handle a sentinel zero.
        if (s.width > 0)
            map[QStringLiteral("width")] = s.width;
        if (s.height > 0)
            map[QStringLiteral("height")] = s.height;
        if (s.width > 0 && s.height > 0)
            map[QStringLiteral("resolution")] = QStringLiteral("%1×%2").arg(s.width).arg(s.height);
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
            const QStringList parts = vendorModelParts(s);
            const QString monitorName = parts.isEmpty() ? s.connectorName : parts.join(QLatin1Char(' '));
            label = monitorName.isEmpty() ? vsName : vsName + QStringLiteral(" — ") + monitorName;
        } else {
            const QStringList parts = vendorModelParts(s);
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
