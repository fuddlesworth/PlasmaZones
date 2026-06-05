// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PhosphorSettingsUi/PhosphorSettingsUi.h"

namespace PhosphorSettingsUi {

QString version()
{
    // PHOSPHORSETTINGSUI_VERSION_STR is set from PHOSPHORSETTINGSUI_VERSION
    // in libs/phosphor-settings-ui/CMakeLists.txt so one bump updates both
    // the install version metadata and this getter.
    return QStringLiteral(PHOSPHORSETTINGSUI_VERSION_STR);
}

} // namespace PhosphorSettingsUi
