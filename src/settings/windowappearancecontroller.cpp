// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windowappearancecontroller.h"

#include "../config/settings.h"

namespace PlasmaZones {

QString WindowAppearanceController::perScreenGapRuleId(const QString& screenId) const
{
    if (screenId.isEmpty()) {
        return QString();
    }
    // Key by the stable EDID form so the id agrees with the v4→v5 migration and
    // the per-screen gap reader, which both key per-monitor gap rules by the
    // canonical stable id rather than the connector name.
    return QUuid::createUuidV5(ConfigDefaults::baselineGapRuleId(), Settings::canonicalPerScreenKey(screenId).toUtf8())
        .toString();
}

QString WindowAppearanceController::canonicalScreenId(const QString& screenId) const
{
    return Settings::canonicalPerScreenKey(screenId);
}

} // namespace PlasmaZones
