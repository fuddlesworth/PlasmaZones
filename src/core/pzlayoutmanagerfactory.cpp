// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pzlayoutmanagerfactory.h"

#include "../config/configbackends.h"
#include "../config/configdefaults.h"

#include <PhosphorZones/LayoutManager.h>

#include <QStandardPaths>

namespace PlasmaZones {

std::unique_ptr<PhosphorZones::LayoutManager> makePzLayoutManager(QObject* parent)
{
    PhosphorZones::LayoutManagerConfig config{
        /*assignmentGroupPrefix=*/ConfigDefaults::assignmentGroupPrefix(),
        /*quickLayoutsGroup=*/ConfigDefaults::quickLayoutsGroup(),
        /*modeTrackingGroup=*/ConfigDefaults::modeTrackingGroup(),
        /*defaultLayoutDirectory=*/QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
            + QStringLiteral("/plasmazones/layouts"),
        /*legacyMigrationBackendFactory=*/
        []() {
            return createDefaultConfigBackend();
        },
    };
    return std::make_unique<PhosphorZones::LayoutManager>(std::move(config), createAssignmentsBackend(), parent);
}

} // namespace PlasmaZones
