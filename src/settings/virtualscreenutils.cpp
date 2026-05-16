// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "virtualscreenutils.h"

#include <PhosphorIdentity/VirtualScreenId.h>

#include <QRectF>
#include <QStringLiteral>

namespace PlasmaZones::VirtualScreenUtils {

Phosphor::Screens::VirtualScreenDef variantMapToVirtualScreenDef(const QVariantMap& map,
                                                                 const QString& physicalScreenId, int index)
{
    Phosphor::Screens::VirtualScreenDef def;
    def.physicalScreenId = physicalScreenId;
    def.index = index;
    def.displayName = map.value(QStringLiteral("displayName")).toString();
    def.region = QRectF(map.value(QStringLiteral("x")).toDouble(), map.value(QStringLiteral("y")).toDouble(),
                        map.value(QStringLiteral("width")).toDouble(), map.value(QStringLiteral("height")).toDouble());
    def.id = PhosphorIdentity::VirtualScreenId::make(physicalScreenId, index);
    return def;
}

} // namespace PlasmaZones::VirtualScreenUtils
