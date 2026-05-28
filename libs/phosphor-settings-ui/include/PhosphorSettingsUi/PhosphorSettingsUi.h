// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QString>

#include "phosphorsettingsui_export.h"

// Umbrella header — pulls in every public type so consumers can
// `#include <PhosphorSettingsUi/PhosphorSettingsUi.h>` and get the
// whole framework's surface at once (ApplicationController,
// PageController, StagingDomain, PageRegistry, DBusBridge,
// LocalizedContext). Single-class headers remain available for
// callers that want to limit what they bring in.
#include "PhosphorSettingsUi/ApplicationController.h"
#include "PhosphorSettingsUi/DBusBridge.h"
#include "PhosphorSettingsUi/LocalizedContext.h"
#include "PhosphorSettingsUi/PageController.h"
#include "PhosphorSettingsUi/PageRegistry.h"
#include "PhosphorSettingsUi/StagingDomain.h"

namespace PhosphorSettingsUi {

PHOSPHORSETTINGSUI_EXPORT QString version();

} // namespace PhosphorSettingsUi
