// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QString>

#include "phosphorcontrol_export.h"

// Umbrella header — pulls in every public type so consumers can
// `#include <PhosphorControl/PhosphorControl.h>` and get the
// whole framework's surface at once (ApplicationController,
// PageController, StagingDomain, PageRegistry, DBusBridge,
// LocalizedContext). Single-class headers remain available for
// callers that want to limit what they bring in.
#include "PhosphorControl/ApplicationController.h"
#include "PhosphorControl/DBusBridge.h"
#include "PhosphorControl/LocalizedContext.h"
#include "PhosphorControl/PageController.h"
#include "PhosphorControl/PageRegistry.h"
#include "PhosphorControl/StagingDomain.h"

namespace PhosphorControl {

PHOSPHORCONTROL_EXPORT QString version();

} // namespace PhosphorControl
