// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// QML payload shape — five changes consumers should audit:
//   1. `width` / `height` keys are emitted independently when only one
//      dimension is positive (previously: all-or-nothing).
//   2. `resolution` is only emitted when BOTH width and height are positive.
//   3. `isVirtualScreen` is ALWAYS present in every map (previously:
//      only when true).
//   4. `x` / `y` (screen-space origin) are ALWAYS present, including 0,0
//      and negative coordinates — unlike width/height they are never
//      skipped, so spatial-arrangement consumers see every monitor.
//   5. `displayLabel` is ALWAYS present — a precomputed make/model + ` (W×H)`
//      label (or `VS<n> — <monitor>` for a virtual screen) with a
//      ` · <connector>` disambiguation suffix; bind it instead of reassembling
//      the name / manufacturer / model / resolution fields.
// QML consumers relying on `Object.keys(map).includes('isVirtualScreen')`
// semantics for physical-vs-virtual disambiguation must be audited; see
// libs/phosphor-screens/CHANGES.md for full rationale.

#include "PhosphorScreens/ScreenInfo.h"
#include "screenslogging.h"

#include <QStringList>

namespace PhosphorScreens {

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
        // contract is virtualIndex >= 0 when isVirtualScreen is true
        // (header docstring). A negative index plus the truthy flag
        // surfaces as "VS0" via `virtualIndex + 1`, which is
        // misleading; warn so producers find the regression instead
        // of silently shipping a wrong tile label.
        if (s.isVirtualScreen && s.virtualIndex < 0) {
            qCWarning(lcPhosphorScreens) << Q_FUNC_INFO << "virtual screen with virtualIndex < 0 (=" << s.virtualIndex
                                         << ") — label will report VS" << (s.virtualIndex + 1);
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
        // Position is always emitted (unlike width/height): 0,0 is a valid
        // screen-space origin, so the >0 skip used for dimensions would
        // wrongly drop a monitor sitting at the coordinate origin from a
        // multi-monitor map.
        map[QStringLiteral("x")] = s.x;
        map[QStringLiteral("y")] = s.y;
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
        const QStringList parts = vendorModelParts(s);
        if (s.isVirtualScreen) {
            const QString vsName =
                s.virtualDisplayName.isEmpty() ? QStringLiteral("VS%1").arg(s.virtualIndex + 1) : s.virtualDisplayName;
            const QString monitorName = parts.isEmpty() ? s.connectorName : parts.join(QLatin1Char(' '));
            label = monitorName.isEmpty() ? vsName : vsName + QStringLiteral(" — ") + monitorName;
        } else {
            label = parts.isEmpty() ? s.name : parts.join(QLatin1Char(' '));
        }
        if (s.width > 0 && s.height > 0) {
            label += QStringLiteral(" (%1×%2)").arg(s.width).arg(s.height);
        }
        // Disambiguate monitors that share a make/model by appending the
        // physical connector — two "LG Ultra HD" panels become "… · DP-1" /
        // "… · HDMI-A-1". This matters for virtual screens too: virtualIndex is
        // per-physical-monitor (VS1/VS2 repeat on every monitor — see
        // ScreenInfo.h), so the connector is what distinguishes "VS1 — LG Ultra
        // HD" on DP-1 from the same on DP-2. Skip only when the connector is
        // already the label's monitor name (no make/model): a virtual screen
        // then reads "VS1 — DP-2" and a physical one "DP-2", so "· DP-2" would
        // be redundant.
        if (!s.connectorName.isEmpty()) {
            const bool connectorAlreadyShown = parts.isEmpty() && (s.isVirtualScreen || s.name == s.connectorName);
            if (!connectorAlreadyShown)
                label += QStringLiteral(" · ") + s.connectorName;
        }
        map[QStringLiteral("displayLabel")] = label;

        list.append(map);
    }

    return list;
}

} // namespace PhosphorScreens
